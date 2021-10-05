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
	m_readBlock = BlockIndex::Y;
	m_writeBlock = BlockIndex::Y;
	m_dataOutputBit15 = false;
	m_dataOutputSigned = false;
	m_dataOutputDepth = DataOutputDepth::Four;
	m_enableDataOut = false;
	m_enableDataIn = false;
	m_color = false;
	m_state = State::Idle;
	m_dataInBuffer.Reset();
	m_dataOutBuffer.Reset();
}

uint32_t MacroblockDecoder::Read( uint32_t offset )
{
	dbExpects( offset < 2 );
	if ( offset == 0 )
		return ReadData();
	else
		return ReadStatus();
}

uint32_t MacroblockDecoder::ReadStatus()
{
	// TODO: check dma setting
	const bool dataOutRequest = m_enableDataOut;
	const bool dataInRequest = m_enableDataIn;
	const uint16_t remainingParamsMinusOne = static_cast<uint16_t>( ( m_remainingHalfWords + 1 ) / 2 - 1 );
	const BlockIndex currentBlock = m_dataOutBuffer.Empty() ? m_readBlock : m_writeBlock;

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
		// TODO: stall the CPU if MDEC has pending block

		// dbLogWarning( "MacroblockDecoder::ReadData -- data out buffer is empty" );
		return 0xffffffffu;
	}
	else
	{
		const uint32_t value = m_dataOutBuffer.Pop();

		// TODO: decode next block if buffer is empty

		return value;
	}
}

void MacroblockDecoder::Write( uint32_t offset, uint32_t value )
{
	dbExpects( offset < 2 );
	if ( offset == 0 )
	{
		WriteParam( value );
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

void MacroblockDecoder::WriteParam( uint32_t value )
{
	m_dataInBuffer.Push( static_cast<uint16_t>( value ) );
	m_dataInBuffer.Push( static_cast<uint16_t>( value >> 16 ) );

	switch ( m_state )
	{
		case State::Idle:
		{
			const uint32_t command = m_dataInBuffer.Pop() | ( m_dataInBuffer.Pop() << 16 );
			StartCommand( command );
			break;
		}

		case State::ReadingMacroblock:
			if ( m_dataInBuffer.Size() == m_remainingHalfWords )
				Decode();
			break;

		case State::DecodingMacroblock:
			dbBreak(); // TODO
			break;

		case State::ReadingQuantTable:
			if ( m_dataInBuffer.Size() == m_remainingHalfWords )
			{
				m_dataInBuffer.Clear();
				m_state = State::Idle;
			}
			// TODO
			break;

		case State::ReadingScaleTable:
			if ( m_dataInBuffer.Size() == m_remainingHalfWords )
			{
				m_dataInBuffer.Clear();
				m_state = State::Idle;
			}
			// TODO
			break;

		case State::InvalidCommand:
		{
			m_remainingHalfWords -= 2;
			if ( m_remainingHalfWords == 0 )
				m_state = State::Idle;

			break;
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
			m_state = State::ReadingMacroblock;
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
			m_state = State::InvalidCommand;
			m_remainingHalfWords = static_cast<uint16_t>( value + 1 ) * 2;
			break;
		}
	}
}

void MacroblockDecoder::Decode()
{
	// TODO

	const uint32_t pixelsPerBlock = m_color ? 256 : 64;

	uint32_t words = 0;
	switch ( m_dataOutputDepth )
	{
		case DataOutputDepth::Four:
			words = pixelsPerBlock / 8;
			break;

		case DataOutputDepth::Eight:
			words = pixelsPerBlock / 4;
			break;

		case DataOutputDepth::TwentyFour:
			words = ( ( pixelsPerBlock * 3 ) + 3 ) / 4;
			break;

		case DataOutputDepth::Fifteen:
			words = pixelsPerBlock / 2;
			break;
	}

	for ( uint32_t i = 0; i < words; ++i )
		m_dataOutBuffer.Push( 0 );

	m_dataInBuffer.Clear();
	m_state = State::Idle;
}

bool MacroblockDecoder::rl_decode_block( int16_t* blk, const uint8_t* qt )
{
	if ( m_currentK == 64 )
	{
		std::fill_n( blk, 64, int16_t( 0 ) );

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

			m_colorDest[ ( x + xx ) + ( y + yy ) * 16 ] = BGR;
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

		m_colorDest[ i ] = static_cast<uint8_t>( Y );
	}
}

}