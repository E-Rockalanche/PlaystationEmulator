#include "MacroblockDecoder.h"

#include "EventManager.h"
#include "DMA.h"
#include "SaveState.h"

#include <stdx/scope.h>

#include <algorithm>

namespace PSX
{

namespace
{

constexpr std::array<uint8_t, 64> CreateZagZig()
{
	constexpr std::array<uint8_t, 64> ZigZag
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

	std::array<uint8_t, 64> result{};
	for ( uint8_t i = 0; i < 64; ++i )
		result[ ZigZag[ i ] ] = i;

	return result;
}

constexpr std::array<uint8_t, 64> ZagZig = CreateZagZig();

constexpr std::array<cycles_t, 4> CyclesPerBlock // values from duckstation
{
	448,		// 4bit
	448,		// 8bit
	448 * 6,	// 24bit
	550 * 6,	// 15bit
};

}

MacroblockDecoder::MacroblockDecoder( EventManager& eventManager )
	: m_eventManager{ eventManager }
{
	m_outputBlockEvent = eventManager.CreateEvent( "MDEC output block", [this]( cycles_t ) { OutputBlock(); } );
}

void MacroblockDecoder::Reset()
{
	m_outputBlockEvent->Reset();

	m_status.value = 0;

	m_remainingHalfWords = 2;

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

	UpdateStatus();
}

void MacroblockDecoder::UpdateStatus()
{
	m_status.remainingParameters = static_cast<uint16_t>( ( m_remainingHalfWords + 1 ) / 2 - 1 );
	m_status.currentBlock = ( m_currentBlock + 4 ) % BlockIndex::Count;
	m_status.commandBusy = ( m_state != State::Idle );
	m_status.dataInFifoFull = m_dataInBuffer.Full();
	m_status.dataOutFifoEmpty = m_dataOutBuffer.Empty();

	const bool dataInRequest = m_enableDataIn && ( m_dataInBuffer.Capacity() >= 64 ); // 8x8 halfwords
	m_status.dataInRequest = dataInRequest;
	m_dma->SetRequest( Dma::Channel::MDecIn, dataInRequest );

	const bool dataOutRequest = m_enableDataOut && !m_dataOutBuffer.Empty();
	m_status.dataOutRequest = dataOutRequest;
	m_dma->SetRequest( Dma::Channel::MDecOut, dataOutRequest );
}

uint32_t MacroblockDecoder::ReadData()
{
	if ( m_dataOutBuffer.Empty() )
	{
		// Duckstation stalls the CPU if a block is being processed
		if ( m_outputBlockEvent->IsActive() )
		{
			dbLogWarning( "MacroblockDecoder::ReadData -- output fifo is empty, stalling CPU until data is processed" );
			m_eventManager.AddCyclesAndUpdateEvents( m_outputBlockEvent->GetRemainingCycles() );
		}
		else
		{
			dbLogWarning( "MacroblockDecoder::ReadData -- output fifo is empty" );
			return 0xffffffffu;
		}
	}

	const uint32_t value = m_dataOutBuffer.Pop();

	// process more data if we were waiting for output fifo to empty
	if ( m_dataOutBuffer.Empty() )
		ProcessInput();
	else
		UpdateStatus();

	return value;
}

void MacroblockDecoder::Write( uint32_t offset, uint32_t value )
{
	dbExpects( offset < 2 );
	if ( offset == 0 )
	{
		// command/parameter register

		m_dataInBuffer.Push( static_cast<uint16_t>( value ) );
		m_dataInBuffer.Push( static_cast<uint16_t>( value >> 16 ) );
		ProcessInput();
	}
	else
	{
		// control/reset register

		if ( value & ( 1u << 31 ) )
		{
			// soft reset
			if ( m_state != State::Idle )
				dbLogWarning( "MacroblockDecoder::Write -- soft reset during state %u", (uint32_t)m_state );

			m_outputBlockEvent->Cancel();
			m_status.value = 0;
			m_remainingHalfWords = 0;
			m_state = State::Idle;
			m_dataInBuffer.Clear();
			m_dataOutBuffer.Clear();
			m_currentK = 64;
			m_currentQ = 0;
			m_currentBlock = 0;
		}

		m_enableDataIn = stdx::any_of( value, 1u << 30 );
		m_enableDataOut = stdx::any_of( value, 1u << 29 );

		UpdateStatus();
	}
}

void MacroblockDecoder::DmaIn( const uint32_t* input, uint32_t count )
{
	if ( m_dataInBuffer.Capacity() < count * 2 )
		dbLogWarning( "MacroblockDecoder::DmaIn -- input buffer overflow" );

	const uint32_t minCount = std::min( m_dataInBuffer.Capacity(), count * 2 );
	m_dataInBuffer.Push( reinterpret_cast<const uint16_t*>( input ), minCount );
	ProcessInput();
}

void MacroblockDecoder::DmaOut( uint32_t* output, uint32_t count )
{
	const uint32_t minCount = std::min( m_dataOutBuffer.Size(), count );
	m_dataOutBuffer.Pop( output, minCount );

	if ( minCount < count )
	{
		dbLogWarning( "MacroblockDecoder::DmaOut -- output fifo is empty" );
		std::fill_n( output + minCount, count - minCount, 0xffffffffu );
	}

	// process more data if we were waiting for output fifo to empty
	if ( m_dataOutBuffer.Empty() )
		ProcessInput();
}

void MacroblockDecoder::ProcessInput()
{
	stdx::scope_exit onExit( [this] { UpdateStatus(); } );

	// keep processing data until there's no more or something returns
	for(;;)
	{
		switch ( m_state )
		{
			case State::Idle:
			{
				if ( m_dataInBuffer.Size() < 2 )
					return;

				const uint32_t command = m_dataInBuffer.Pop() | ( m_dataInBuffer.Pop() << 16 );
				StartCommand( command );
				break;
			}

			case State::DecodingMacroblock:
			{
				if ( DecodeMacroblock() )
				{
					ScheduleOutput();
					return; // block needs to be read
				}
				else if ( m_remainingHalfWords == 0 && m_currentBlock != BlockIndex::Count )
				{
					// didn't get enough data to decode all blocks. Probably because of dummy data at the end
					m_currentBlock = 0;
					m_currentK = 64;
					m_state = State::Idle;
				}
				else
				{
					return; // need more data
				}
				break;
			}

			case State::WritingMacroblock:
				// wait until block is ready to output
				return;

			case State::ReadingQuantTable:
			{
				if ( m_dataInBuffer.Size() < m_remainingHalfWords )
					return;

				m_dataInBuffer.Pop( reinterpret_cast<uint16_t*>( m_luminanceTable.data() ), 32 ); // 64 bytes
				if ( m_color )
					m_dataInBuffer.Pop( reinterpret_cast<uint16_t*>( m_colorTable.data() ), 32 ); // 64 bytes

				m_remainingHalfWords = 0;
				m_state = State::Idle;
				break;
			}

			case State::ReadingScaleTable:
			{
				if ( m_dataInBuffer.Size() < m_remainingHalfWords )
					return;

				m_dataInBuffer.Pop( reinterpret_cast<uint16_t*>( m_scaleTable.data() ), 64 );

				m_remainingHalfWords = 0;
				m_state = State::Idle;
				break;
			}
		}
	}
}

void MacroblockDecoder::StartCommand( uint32_t value )
{
	union CommandWord
	{
		CommandWord( uint32_t v ) : value{ v } {}

		struct
		{
			uint32_t parameterWords : 16;
			uint32_t : 9;
			uint32_t dataOutputBit15 : 1;
			uint32_t dataOutputSigned : 1;
			uint32_t dataOutputDepth : 2;
			uint32_t command : 3;
		};
		uint32_t value;
	};
	static_assert( sizeof( CommandWord ) == 4 );

	const CommandWord commandWord{ value };

	m_status.dataOutputBit15 = commandWord.dataOutputBit15;
	m_status.dataOutputSigned = commandWord.dataOutputSigned;
	m_status.dataOutputDepth = commandWord.dataOutputDepth;

	switch ( static_cast<Command>( commandWord.command ) )
	{
		case Command::DecodeMacroblock:
		{
			dbLogDebug( "MacroblockDecoder::StartCommand -- DecodeMacroblock [%u]", commandWord.parameterWords );
			m_state = State::DecodingMacroblock;
			m_remainingHalfWords = commandWord.parameterWords * 2;
			break;
		}

		case Command::SetQuantTable:
		{
			// The command word is followed by 64 unsigned parameter bytes for the Luminance Quant Table (used for Y1..Y4),
			// and if Command.Bit0 was set, by another 64 unsigned parameter bytes for the Color Quant Table (used for Cb and Cr).
			dbLogDebug( "MacroblockDecoder::StartCommand -- SetQuantTable [color=%i]", value & 0x01 );
			m_state = State::ReadingQuantTable;
			m_color = value & 0x01;
			m_remainingHalfWords = ( 1 + m_color ) * 32;
			break;
		}

		case Command::SetScaleTable:
		{
			// The command is followed by 64 signed halfwords with 14bit fractional part, the values should be usually/always the same values
			// (based on the standard JPEG constants, although, MDEC(3) allows to use other values than that constants).
			dbLogDebug( "MacroblockDecoder::StartCommand -- SetScaleTable" );
			m_state = State::ReadingScaleTable;
			m_remainingHalfWords = 64;
			break;
		}

		case Command::NoFunction:
		default:
		{
			// This command has no function. Command bits 25-28 are reflected to Status bits 23-26 as usually.
			// Command bits 0-15 are reflected to Status bits 0-15 (similar as the "number of parameter words" for MDEC(1),
			// but without the "minus 1" effect, and without actually expecting any parameters).
			dbBreakMessage( "MacroblockDecoder::StartCommand -- NoFunction [%08X]", value );
			m_status.remainingParameters = commandWord.parameterWords;
			break;
		}
	}
}

bool MacroblockDecoder::DecodeColoredMacroblock()
{
	// decode remaining blocks
	while ( m_currentBlock < BlockIndex::Count )
	{
		auto& table = ( m_currentBlock < 2 ) ? m_colorTable : m_luminanceTable;
		if ( !rl_decode_block( m_blocks[ m_currentBlock ], table ) )
			return false;

		real_idct_core( m_blocks[ m_currentBlock ] );
		++m_currentBlock;
	}

	// wait for output fifo to be emptied
	if ( !m_dataOutBuffer.Empty() )
		return false;

	// calculate final rgb data
	yuv_to_rgb( 0, 0, m_blocks[ BlockIndex::Cr ], m_blocks[ BlockIndex::Cb ], m_blocks[ BlockIndex::Y1 ] ); // upper left
	yuv_to_rgb( 8, 0, m_blocks[ BlockIndex::Cr ], m_blocks[ BlockIndex::Cb ], m_blocks[ BlockIndex::Y2 ] ); // upper right (nocash says 0, 8?)
	yuv_to_rgb( 0, 8, m_blocks[ BlockIndex::Cr ], m_blocks[ BlockIndex::Cb ], m_blocks[ BlockIndex::Y3 ] ); // lower left (nocash says 8, 0?)
	yuv_to_rgb( 8, 8, m_blocks[ BlockIndex::Cr ], m_blocks[ BlockIndex::Cb ], m_blocks[ BlockIndex::Y4 ] ); // lower right

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

	real_idct_core( m_blocks[ BlockIndex::Y ] );

	// calculate final greyscale
	y_to_mono( m_blocks[ BlockIndex::Y ] );

	return true;
}

bool MacroblockDecoder::rl_decode_block( Block& blk, const Table& qt )
{
	if ( m_currentK == 64 )
	{
		blk.fill( 0 );

		// skip padding
		uint16_t n = 0;
		do
		{
			if ( m_dataInBuffer.Empty() || m_remainingHalfWords == 0 )
				return false;

			n = m_dataInBuffer.Pop();
			--m_remainingHalfWords;
		}
		while ( n == EndOfBlock );

		// start filling block
		m_currentK = 0;

		m_currentQ = ( n >> 10 ) & 0x3f;
		int32_t val = SignExtend<10, int32_t>( n ) * static_cast<int32_t>( qt[ m_currentK ] );

		if ( m_currentQ == 0 )
			val = SignExtend<10, int32_t>( n ) * 2;

		val = std::clamp<int32_t>( val, -0x400, 0x3ff );

		if ( m_currentQ > 0 )
			blk[ ZagZig[ m_currentK ] ] = static_cast<int16_t>( val );
		else if ( m_currentQ == 0 )
			blk[ m_currentK ] = static_cast<int16_t>( val );
	}

	while( !m_dataInBuffer.Empty() && m_remainingHalfWords > 0 && m_currentK < 63 )
	{
		const uint16_t n = m_dataInBuffer.Pop();
		--m_remainingHalfWords;

		m_currentK += ( ( n >> 10 ) & 0x3f ) + 1;

		if ( m_currentK >= 64 )
		{
			// finished filling block
			m_currentK = 64;
			return true;
		}

		int32_t val = ( SignExtend<10, int32_t>( n ) * static_cast<int32_t>( qt[ m_currentK ] ) * static_cast<int32_t>( m_currentQ ) + 4 ) / 8;

		if ( m_currentQ == 0 )
			val = SignExtend<10, int32_t>( n ) * 2;

		val = std::clamp( val, -0x400, 0x3ff );
		if ( m_currentQ > 0 )
			blk[ ZagZig[ m_currentK ] ] = static_cast<int16_t>( val );
		else if ( m_currentQ == 0 )
			blk[ m_currentK ] = static_cast<int16_t>( val );
	}

	if ( m_currentK == 63 )
	{
		// block was fully defined
		m_currentK = 64;
		return true;
	}

	return false;
}

void MacroblockDecoder::real_idct_core( Block& blk )
{
	Block tempBuffer;

	int16_t* src = blk.data();
	int16_t* dst = tempBuffer.data();

	for ( size_t pass = 0; pass < 2; ++pass )
	{
		for ( size_t x = 0; x < 8; ++x )
		{
			for ( size_t y = 0; y < 8; ++y )
			{
				int64_t sum = 0;
				for ( size_t z = 0; z < 8; ++z )
				{
					sum += static_cast<int64_t>( src[ y + z * 8 ] ) * ( m_scaleTable[ x + z * 8 ] / 8 );
				}
				dst[ x + y * 8 ] = static_cast<int16_t>( ( sum + 0xfff ) / 0x2000 ); // or so?
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
			const size_t crbIndex = ( ( x + xx ) / 2 ) + ( ( y + yy ) / 2 ) * 8; // sample 8x8 color table from 16x16 coordinates

			int16_t R = crBlk[ crbIndex ];
			int16_t B = cbBlk[ crbIndex ];
			int16_t G = static_cast<int16_t>( -0.3437f * B + -0.7143f * R );
			R = static_cast<int16_t>( 1.402f * R );
			B = static_cast<int16_t>( 1.772f * B );

			int16_t Y = yBlk[ x + y * 8 ];
			R = static_cast<int16_t>( std::clamp<int32_t>( Y + R, -128, 127 ) );
			G = static_cast<int16_t>( std::clamp<int32_t>( Y + G, -128, 127 ) );
			B = static_cast<int16_t>( std::clamp<int32_t>( Y + B, -128, 127 ) );

			if ( !m_status.dataOutputSigned )
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
		int16_t Y = yBlk[ i ];
		Y = static_cast<int16_t>( SignExtend<9, int16_t>( Y ) ); // clip to signed 9bit range
		Y = std::clamp<int16_t>( Y, -128, 127 ); // saturate from 9bit to signed 8bit range

		if ( !m_status.dataOutputSigned )
			Y += 128;

		m_dest[ i ] = static_cast<uint32_t>( Y & 0xff );
	}
}

void MacroblockDecoder::ScheduleOutput()
{
	dbExpects( m_state == State::DecodingMacroblock );
	dbExpects( !m_outputBlockEvent->IsActive() );

	m_state = State::WritingMacroblock;
	m_outputBlockEvent->Schedule( CyclesPerBlock[ m_status.dataOutputDepth ] );
}

void MacroblockDecoder::OutputBlock()
{
	dbExpects( m_state == State::WritingMacroblock );
	dbExpects( m_dataOutBuffer.Empty() );

	m_outputBlockEvent->Cancel(); // we need to schedule the next event here because the DMA will take control and add cycles

	switch ( static_cast<DataOutputDepth>( m_status.dataOutputDepth ) )
	{
		case DataOutputDepth::Four: // mono
		{
			auto to4bit = []( uint32_t luminance ) { return ( luminance >> 4 ); }; // convert 8bit luminance to 4bit

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

		case DataOutputDepth::TwentyFour: // color
		{
			uint32_t value = 0;
			int curSize = 0;
			for ( size_t i = 0; i < m_dest.size(); ++i )
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
			dbAssert( curSize == 0 );
			break;
		}

		case DataOutputDepth::Fifteen: // color
		{
			const uint16_t maskBit = m_status.dataOutputBit15 ? 0x8000 : 0;

			auto toBGR15 = [maskBit]( uint32_t BGR24 ) -> uint16_t
			{
				auto to5bit = []( uint32_t c ) { return static_cast<uint16_t>( ( c >> 3 ) & 0x1f ); };

				const uint16_t red = to5bit( BGR24 );
				const uint16_t green = to5bit( BGR24 >> 8 );
				const uint16_t blue = to5bit( BGR24 >> 16 );
				return red | ( green << 5 ) | ( blue << 10 ) | maskBit;
			};

			for ( size_t i = 0; i < m_dest.size(); i += 2 )
			{
				const uint32_t value = toBGR15( m_dest[ i ] ) | ( toBGR15( m_dest[ i + 1 ] ) << 16 );
				m_dataOutBuffer.Push( value );
			}
			break;
		}
	}

	dbLogDebug( "MacroblockDecoder::OutputBlock -- remaining half-words: %X", m_remainingHalfWords );

	m_state = ( m_remainingHalfWords == 0 ) ? State::Idle : State::DecodingMacroblock;
	ProcessInput();
}

void MacroblockDecoder::Serialize( SaveStateSerializer& serializer )
{
	if ( !serializer.Header( "MDEC", 1 ) )
		return;

	m_outputBlockEvent->Serialize( serializer );

	serializer( m_status.value );

	serializer( m_remainingHalfWords );

	serializer( m_enableDataOut );
	serializer( m_enableDataIn );
	serializer( m_color );

	serializer( m_state );

	serializer( m_dataInBuffer );
	serializer( m_dataOutBuffer );

	serializer( m_luminanceTable );
	serializer( m_colorTable );
	serializer( m_scaleTable );

	serializer( m_currentK );
	serializer( m_currentQ );

	serializer( m_blocks );
	serializer( m_currentBlock );

	serializer( m_dest );
}

}