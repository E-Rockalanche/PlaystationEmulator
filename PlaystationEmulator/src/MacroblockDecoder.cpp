#include "MacroblockDecoder.h"

#include <algorithm>

namespace PSX
{

namespace
{

const std::array<uint32_t, 64> ZigZag
{
	0,  1,  5,  6,  14, 15, 27, 28,
	2,  4,  7,  13, 16, 26, 29, 42,
	3,  8,  12, 17, 25, 30, 41, 43,
	9,  11, 18, 24, 31, 40, 44, 53,
	10, 19, 23, 32, 39, 45, 52, 54,
	20, 22, 33, 38, 46, 51, 55, 60,
	21, 34, 37, 47, 50, 56, 59, 61,
	35, 36, 48, 49, 57, 58, 62, 63
};

constexpr int16_t SignExtend10( uint16_t value ) noexcept
{
	return static_cast<int16_t>( ( value & 0x03ffu ) | ( value & 0x200u ) ? 0xfc00u : 0u );
}

}

void MacroblockDecoder::Reset()
{
	m_remainingHalfWords = 2;

	m_dataOutputBit15 = false;
	m_dataOutputSigned = false;

	m_dataOutputDepth = DataOutputDepth::Four;

	m_enableDataOut = false;
	m_enableDataIn = false;
	m_color = false;

	m_state = State::Idle;

	m_dataInBuffer.Reset();
	m_dataOutBuffer.Reset();

	m_luminanceTable.fill( 0 );

	m_colorTable.fill( 0 );
	m_scaleTable.fill( 0 );

	m_currentK = 64;
	m_currentQ = 0;

	for ( auto& block : m_blocks )
		block.fill( 0 );

	m_currentBlock = 0;
	m_dest.fill( 0 );
}

uint32_t MacroblockDecoder::ReadStatus()
{
	// TODO: check dma setting
	const bool dataOutRequest = m_enableDataOut;
	const bool dataInRequest = m_enableDataIn;
	const uint16_t remainingParamsMinusOne = static_cast<uint16_t>( ( m_remainingHalfWords + 1 ) / 2 - 1 );
	const uint16_t currentBlock = ( m_currentBlock + 4 ) % BlockIndex::Count;

	return static_cast<uint32_t>( remainingParamsMinusOne ) |
		( static_cast<uint32_t>( currentBlock ) << 16 ) |
		( static_cast<uint32_t>( m_dataOutputBit15 ) << 23 ) |
		( static_cast<uint32_t>( m_dataOutputDepth ) << 25 ) |
		( static_cast<uint32_t>( dataOutRequest ) << 27 ) |
		( static_cast<uint32_t>( dataInRequest ) << 28 ) |
		( static_cast<uint32_t>( m_state != State::Idle ) << 29 ) |
		( static_cast<uint16_t>( m_dataInBuffer.Full() ) << 30 ) |
		( static_cast<uint16_t>( m_dataOutBuffer.Empty() ) << 31 );
}

uint32_t MacroblockDecoder::ReadData()
{
	if ( m_dataOutBuffer.Empty() )
	{
		// TODO: stall the CPU if MDEC has pending block?

		dbLogWarning( "MacroblockDecoder::ReadData -- output fifo is empty" );
		return 0xffffffffu;
	}
	else
	{
		const uint32_t value = m_dataOutBuffer.Pop();

		// process more data if we were waiting for output fifo to empty
		if ( m_dataOutBuffer.Empty() && ( m_state == State::DecodingMacroblock ) )
			ProcessInput();

		return value;
	}
}

void MacroblockDecoder::Write( uint32_t offset, uint32_t value )
{
	dbExpects( offset < 2 );
	if ( offset == 0 )
	{
		m_dataInBuffer.Push( static_cast<uint16_t>( value ) );
		m_dataInBuffer.Push( static_cast<uint16_t>( value >> 16 ) );
		ProcessInput();
	}
	else
	{
		// control/reset
		if ( value & ( 1u << 31 ) )
		{
			m_state = State::Idle;
			// TODO: reset status
		}

		m_enableDataIn = stdx::any_of( value, 1u << 30 );
		m_enableDataOut = stdx::any_of( value, 1u << 29 );
	}
}

void MacroblockDecoder::DmaIn( const uint32_t* input, uint32_t count )
{
	m_dataInBuffer.Push( reinterpret_cast<const uint16_t*>( input ), count * 2 );
	ProcessInput();
}

void MacroblockDecoder::DmaOut( uint32_t* output, uint32_t count )
{
	const uint32_t available = std::min( m_dataOutBuffer.Size(), count );
	m_dataOutBuffer.Pop( output, available );

	if ( available < count )
	{
		dbLogWarning( "MacroblockDecoder::DmaOut -- output fifo is empty" );
		std::fill_n( output + available, count - available, 0xffffffffu );
	}

	// process more data if we were waiting for output fifo to empty
	if ( m_dataOutBuffer.Empty() && ( m_state == State::DecodingMacroblock ) )
		ProcessInput();
}

void MacroblockDecoder::ProcessInput()
{
	// keep processing data until there's no more or something returns
	while ( !m_dataInBuffer.Empty() )
	{
		switch ( m_state )
		{
			case State::Idle:
			{
				dbAssert( m_dataInBuffer.Size() >= 2 );
				const uint32_t command = m_dataInBuffer.Pop() | ( m_dataInBuffer.Pop() << 16 );
				StartCommand( command );
				break;
			}

			case State::DecodingMacroblock:
			{
				if ( DecodeMacroblock() )
				{
					m_state = State::WritingMacroblock;
					OutputBlock(); // TODO: schedule output
					return;
				}
				else if ( m_remainingHalfWords == 0 && m_currentBlock != BlockIndex::Count )
				{
					// didn't get enough data to decode all blocks
					m_currentBlock = 0;
					m_currentK = 64;
					m_state = State::Idle;
				}
				break;
			}

			case State::WritingMacroblock:
				// wait until macroblock is in output buffer
				return;

			case State::ReadingQuantTable:
			{
				if ( m_dataInBuffer.Size() < m_remainingHalfWords )
					return;

				m_dataInBuffer.Pop( (uint16_t*)m_luminanceTable.data(), 32 ); // 64 bytes
				if ( m_color )
					m_dataInBuffer.Pop( (uint16_t*)m_colorTable.data(), 32 ); // 64 bytes

				m_remainingHalfWords = 0;
				m_state = State::Idle;
				break;
			}

			case State::ReadingScaleTable:
			{
				if ( m_dataInBuffer.Size() < m_remainingHalfWords )
					return;

				m_dataInBuffer.Pop( (uint16_t*)m_scaleTable.data(), 64 );

				m_remainingHalfWords = 0;
				m_state = State::Idle;
				break;
			}

			case State::InvalidCommand:
			{
				const auto ignoreCount = std::min( m_dataInBuffer.Size(), m_remainingHalfWords );
				m_remainingHalfWords -= ignoreCount;
				m_dataInBuffer.Ignore( ignoreCount );

				if ( m_remainingHalfWords == 0 )
					m_state = State::Idle;

				break;
			}
		}
	}
}

void MacroblockDecoder::StartCommand( uint32_t value )
{
	m_dataOutputBit15 = stdx::any_of( value, 1u << 25 );
	m_dataOutputSigned = stdx::any_of( value, 1u << 26 );
	m_dataOutputDepth = static_cast<DataOutputDepth>( ( value >> 26 ) & 0x3 );

	switch ( static_cast<Command>( value >> 29 ) )
	{
		case Command::DecodeMacroblock:
		{
			m_state = State::DecodingMacroblock;
			m_remainingHalfWords = static_cast<uint16_t>( value ) * 2;
			break;
		}

		case Command::SetQuantTable:
		{
			// The command word is followed by 64 unsigned parameter bytes for the Luminance Quant Table (used for Y1..Y4),
			// and if Command.Bit0 was set, by another 64 unsigned parameter bytes for the Color Quant Table (used for Cb and Cr).
			m_state = State::ReadingQuantTable;
			m_color = stdx::any_of<uint32_t>( value, 1 );
			m_remainingHalfWords = ( 1 + m_color ) * 64 / 2;
			break;
		}

		case Command::SetScaleTable:
		{
			// The command is followed by 64 signed halfwords with 14bit fractional part, the values should be usually/always the same values
			// (based on the standard JPEG constants, although, MDEC(3) allows to use other values than that constants).
			m_state = State::ReadingScaleTable;
			m_remainingHalfWords = 64;
			break;
		}

		default:
		{
			// similar as the "number of parameter words" for MDEC(1), but without the "minus 1" effect, and without actually expecting any parameters
			dbLogWarning( "MacroblockDecoder::StartCommand -- invalid command [%X]", value );
			m_state = State::InvalidCommand;
			m_remainingHalfWords = static_cast<uint16_t>( value + 1 ) * 2;
			break;
		}
	}
}

bool MacroblockDecoder::DecodeColoredMacroblock()
{
	// decode remaining blocks
	for ( auto i = m_currentBlock; i < BlockIndex::Count; ++i )
	{
		if ( !rl_decode_block( m_blocks[ i ], i < 2 ? m_luminanceTable : m_colorTable ) )
			return false;

		real_idct_core( m_blocks[ i ] );
	}

	// wait for output fifo to be emptied
	if ( m_dataOutBuffer.Empty() )
		return false;

	// calculate final rgb data
	yuv_to_rgb( 0, 0, m_blocks[ BlockIndex::Cr ], m_blocks[ BlockIndex::Cb ], m_blocks[ BlockIndex::Y1 ] );
	yuv_to_rgb( 0, 8, m_blocks[ BlockIndex::Cr ], m_blocks[ BlockIndex::Cb ], m_blocks[ BlockIndex::Y2 ] );
	yuv_to_rgb( 8, 0, m_blocks[ BlockIndex::Cr ], m_blocks[ BlockIndex::Cb ], m_blocks[ BlockIndex::Y3 ] );
	yuv_to_rgb( 8, 8, m_blocks[ BlockIndex::Cr ], m_blocks[ BlockIndex::Cb ], m_blocks[ BlockIndex::Y4 ] );

	// reset block
	m_currentBlock = 0;

	return true;
}

bool MacroblockDecoder::DecodeMonoMacroblock()
{
	// wait for output fifo to be emptied
	if ( !m_dataOutBuffer.Empty() )
		return false;

	// decode Y block
	if ( !rl_decode_block( m_blocks[ BlockIndex::Y ], m_luminanceTable ) )
		return false;

	// calculate final greyscale
	y_to_mono( m_blocks[ BlockIndex::Y ] );
	OutputBlock();

	return true;
}

void MacroblockDecoder::OutputBlock()
{
	dbExpects( m_state == State::WritingMacroblock );

	switch ( m_dataOutputDepth )
	{
		case DataOutputDepth::Four: // mono
		{
			auto to4bit = []( uint32_t mono ) { return ( mono >> 4 ); }; // convert 8bit luminance to 4bit

			for ( size_t i = 0; i < 64; i += 8 )
			{
				uint32_t value = to4bit( m_dest[ i ] );
				value |= to4bit( m_dest[ i + 1 ] ) << 4;
				value |= to4bit( m_dest[ i + 2 ] ) << 8;
				value |= to4bit( m_dest[ i + 3 ] ) << 12;
				value |= to4bit( m_dest[ i + 4 ] ) << 16;
				value |= to4bit( m_dest[ i + 5 ] ) << 20;
				value |= to4bit( m_dest[ i + 6 ] ) << 24;
				value |= to4bit( m_dest[ i + 7 ] ) << 28;
				m_dataOutBuffer.Push( value );
			}
			break;
		}

		case DataOutputDepth::Eight: // mono
		{
			for ( size_t i = 0; i < 64; i += 4 )
			{
				uint32_t value = m_dest[ i ];
				value |= m_dest[ i + 1 ] << 8;
				value |= m_dest[ i + 2 ] << 16;
				value |= m_dest[ i + 3 ] << 24;
				m_dataOutBuffer.Push( value );
			}
			break;
		}

		case DataOutputDepth::Fifteen: // color
		{
			const uint32_t maskBit = m_dataOutputBit15 ? 0x8000 : 0;

			auto toBGR15 = [maskBit]( uint32_t BGR24 ) -> uint32_t
			{
				auto to5bit = []( uint32_t c ) { return static_cast<uint16_t>( ( c >> 3 ) & 0x1f ); };

				const uint16_t red = to5bit( BGR24 );
				const uint16_t green = to5bit( BGR24 >> 8 );
				const uint16_t blue = to5bit( BGR24 >> 16 );
				return red | ( green << 5 ) | ( blue << 5 ) | maskBit;
			};

			for ( size_t i = 0; i < 64; i += 2 )
			{
				const uint32_t value = toBGR15( m_dest[ i ] ) | ( toBGR15( m_dest[ i + 1 ] ) << 16 );
				m_dataOutBuffer.Push( value );
			}
			break;
		}

		case DataOutputDepth::TwentyFour: // color
		{
			uint32_t value = 0;
			int curSize = 0;
			for ( size_t i = 0; i < 64; ++i )
			{
				const uint32_t BGR = m_dest[ i ];
				switch ( curSize )
				{
					case 0:
					{
						value = BGR;
						curSize = 3;
						break;
					}

					case 3:
					{
						value |= BGR << 24;
						m_dataOutBuffer.Push( value );
						value = BGR >> 8;
						curSize = 2;
						break;
					}

					case 2:
					{
						value |= BGR << 16;
						m_dataOutBuffer.Push( value );
						value = BGR >> 16;
						curSize = 1;
						break;
					}

					case 1:
					{
						value |= BGR << 8;
						m_dataOutBuffer.Push( value );
						curSize = 0;
						break;
					}
				}
			}
			// there will always be a component left over since 64 is not divisible by 3
			m_dataOutBuffer.Push( value );
			break;
		}
	}

	m_state = ( m_remainingHalfWords == 0 ) ? State::Idle : State::DecodingMacroblock;

	// TODO: uncomment when output is scheduled
	// ProcessInput();
}

bool MacroblockDecoder::rl_decode_block( Block& blk, const Table& qt )
{
	if ( m_currentK == 64 )
	{
		blk.fill( 0 );

		// skip padding
		uint16_t n;
		do
		{
			if ( m_dataInBuffer.Empty() || m_remainingHalfWords == 0 )
				return false;

			n = m_dataInBuffer.Pop();
			--m_remainingHalfWords;
		}
		while ( n == EndOfData );

		// start filling block
		m_currentK = 0;

		const uint16_t q_scale = ( n >> 10 ) & 0x3f;
		int val = SignExtend10( n ) * static_cast<int>( qt[ m_currentK ] );

		if ( m_currentQ == 0 )
			val = SignExtend10( n ) * 2;

		val = std::clamp( val, -0x400, 0x3ff );
		if ( m_currentQ > 0 )
			blk[ ZigZag[ m_currentK ] ] = static_cast<int16_t>( val );
		else
			blk[ m_currentK ] = static_cast<int16_t>( val );
	}

	while( !m_dataInBuffer.Empty() && m_remainingHalfWords > 0 )
	{
		uint16_t n = m_dataInBuffer.Pop();
		--m_remainingHalfWords;

		m_currentK += 1 + ( ( n >> 10 ) & 0x3f );

		if ( m_currentK >= 64 )
		{
			// finished filling block
			m_currentK = 64;
			return true;
		}

		int val = ( SignExtend10( n ) * qt[ m_currentK ] * m_currentQ + 4 ) / 8;

		if ( m_currentQ == 0 )
			val = SignExtend10( n ) * 2;

		val = std::clamp( val, -0x400, 0x3ff );
		if ( m_currentQ > 0 )
			blk[ ZigZag[ m_currentK ] ] = static_cast<int16_t>( val );
		else
			blk[ m_currentK ] = static_cast<int16_t>( val );
	}

	return false;
}

void MacroblockDecoder::real_idct_core( Block& blk )
{
	std::array<int16_t, 64> temp_buffer;

	int16_t* src = blk.data();
	int16_t* dst = temp_buffer.data();

	for ( int pass = 0; pass < 2; ++pass )
	{
		for ( size_t x = 0; x < 8; ++x )
		{
			for ( size_t y = 0; y < 8; ++y )
			{
				int16_t sum = 0;
				for ( size_t z = 0; z < 8; ++z )
				{
					sum = sum + src[ y + z * 8 ] * ( m_scaleTable[ x + z * 8 ] >> 3 );
				}
				dst[ x + y * 8 ] = ( sum + 0xfff ) >> 13;
			}
		}
		std::swap( src, dst );
	}
}

void MacroblockDecoder::yuv_to_rgb( size_t xx, size_t yy, const Block& crBlk, const Block& cbBlk, const Block& yBlk )
{
	for ( size_t y = 0; y < 8; ++y )
	{
		for ( size_t x = 0; x < 8; ++x )
		{
			int R = crBlk[ ( ( x + xx ) / 2 ) + ( ( y + yy ) / 2 ) * 8 ];
			int B = cbBlk[ ( ( x + xx ) / 2 ) + ( ( y + yy ) / 2 ) * 8 ];
			int G = ( B * -22524 + R * -46811 ) / 0xffff;
			R = ( R * 91880 ) / 0xffff;
			B = ( B * 116128 ) / 0xffff;
			int Y = yBlk[ x + y * 8 ];
			R = std::clamp( Y + R, -128, 127 );
			G = std::clamp( Y + G, -128, 127 );
			B = std::clamp( Y + B, -128, 127 );

			if ( !m_dataOutputSigned )
			{
				R += 128;
				G += 128;
				B += 128;
			}

			const uint32_t BGR = ( static_cast<uint8_t>( B ) << 16 ) | ( static_cast<uint8_t>( G ) << 8 ) | static_cast<uint8_t>( R );

			m_dest[ ( x + xx ) + ( y + yy ) * 16 ] = BGR;
		}
	}
}

void MacroblockDecoder::y_to_mono( const Block& yBlk )
{
	for ( size_t i = 0; i < 64; ++i )
	{
		int Y = yBlk[ i ];
		Y = SignExtend10( static_cast<int16_t>( Y ) );
		Y = std::clamp( Y, -128, 127 );

		if ( !m_dataOutputSigned )
			Y += 128;

		m_dest[ i ] = static_cast<uint8_t>( Y );
	}
}

}