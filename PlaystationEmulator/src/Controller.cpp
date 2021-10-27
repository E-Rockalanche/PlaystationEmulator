#include "Controller.h"

namespace PSX
{

bool Controller::Communicate( uint8_t input, uint8_t& output )
{
	switch ( m_state )
	{
		case State::Idle:
		{
			output = 0xff;
			if ( input == 0x01 )
			{
				m_state = State::IdLow;
				return true;
			}
			return false;
		}

		case State::IdLow:
		{
			if ( input == 'B' )
			{
				output = static_cast<uint8_t>( ID );
				m_state = State::IdHigh;
				return true;
			}
			else
			{
				output = 0xff;
				return false;
			}
		}

		case State::IdHigh:
		{
			output = static_cast<uint8_t>( static_cast<uint16_t>( ID ) >> 8 );
			m_state = State::ButtonsLow;
			return true;
		}

		case State::ButtonsLow:
		{
			output = static_cast<uint8_t>( m_buttons );
			m_state = State::ButtonsHigh;
			return true;
		}

		case State::ButtonsHigh:
		{
			output = static_cast<uint8_t>( m_buttons >> 8 );
			m_state = State::Idle;
			return false;
		}

		default:
			dbBreak();
			return false;
	}
}

}