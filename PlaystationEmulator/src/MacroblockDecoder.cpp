#include "MacroblockDecoder.h"

namespace PSX
{


void MacroblockDecoder::Reset()
{
	m_remainingParams = 1;
	m_readBlock = Block::Y;
	m_writeBlock = Block::Y;
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
	const uint16_t remainingParams = static_cast<uint16_t>( m_remainingParams - 1 );
	const Block currentBlock = m_dataOutBuffer.Empty() ? m_readBlock : m_writeBlock;

	return static_cast<uint32_t>( remainingParams ) |
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
	auto pushWord = [&]
	{
		m_dataInBuffer.Push( static_cast<uint16_t>( value ) );
		m_dataInBuffer.Push( static_cast<uint16_t>( value >> 16 ) );
		m_remainingParams--;
	};

	switch ( m_state )
	{
		case State::Idle:
			StartCommand( value );
			break;

		case State::ReadingMacroblock:
			pushWord();
			if ( m_remainingParams == 0 )
				Decode();
			break;

		case State::DecodingMacroblock:
			dbBreak(); // TODO
			pushWord();
			// TODO
			break;

		case State::ReadingQuantTable:
			pushWord();
			if ( m_remainingParams == 0 )
			{
				m_dataInBuffer.Clear();
				m_state = State::Idle;
			}
			// TODO
			break;

		case State::ReadingScaleTable:
			pushWord();
			if ( m_remainingParams == 0 )
			{
				m_dataInBuffer.Clear();
				m_state = State::Idle;
			}
			// TODO
			break;

		case State::InvalidCommand:
		{
			if ( --m_remainingParams == 0 )
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
			m_remainingParams = static_cast<uint16_t>( value );
			dbAssert( m_remainingParams != 0 ); // zero parameters?
			break;
		}

		case Command::SetQuantTable:
		{
			// The command word is followed by 64 unsigned parameter bytes for the Luminance Quant Table (used for Y1..Y4),
			// and if Command.Bit0 was set, by another 64 unsigned parameter bytes for the Color Quant Table (used for Cb and Cr).
			m_state = State::ReadingQuantTable;
			m_color = stdx::any_of<uint32_t>( value, 1 );
			m_remainingParams = ( 1 + m_color ) * 64 / 4;
			break;
		}

		case Command::SetScaleTable:
		{
			// The command is followed by 64 signed halfwords with 14bit fractional part, the values should be usually/always the same values
			// (based on the standard JPEG constants, although, MDEC(3) allows to use other values than that constants).
			m_state = State::ReadingScaleTable;
			m_remainingParams = 64 / 2;
			break;
		}

		default:
		{
			// similar as the "number of parameter words" for MDEC(1), but without the "minus 1" effect, and without actually expecting any parameters
			m_state = State::InvalidCommand;
			m_remainingParams = static_cast<uint16_t>( value + 1 );
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

}