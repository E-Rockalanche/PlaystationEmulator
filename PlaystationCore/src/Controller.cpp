#include "Controller.h"

#include "SaveState.h"

namespace PSX
{

bool Controller::Communicate( uint8_t input, uint8_t& output )
{
	switch ( m_state )
	{
		case State::Idle:
		{
			output = HighZ;
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
				output = static_cast<uint8_t>( GetId() & 0xff );
				m_state = State::IdHigh;
				return true;
			}
			else
			{
				output = HighZ;
				return false;
			}
		}

		case State::IdHigh:
		{
			output = static_cast<uint8_t>( GetId() >> 8 );
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
			if ( m_analogMode )
			{
				m_state = State::JoyRightX;
				return true;
			}
			else
			{
				m_state = State::Idle;
				return false;
			}
		}

		case State::JoyRightX:
		{
			output = m_joyRightX;
			m_state = State::JoyRightY;
			return true;
		}

		case State::JoyRightY:
		{
			output = m_joyRightY;
			m_state = State::JoyLeftX;
			return true;
		}

		case State::JoyLeftX:
		{
			output = m_joyLeftX;
			m_state = State::JoyLeftY;
			return true;
		}

		case State::JoyLeftY:
		{
			output = m_joyLeftY;
			m_state = State::Idle;
			return false;
		}
	}

	dbBreak();
	output = HighZ;
	return false;
}


void Controller::Serialize( SaveStateSerializer& serializer )
{
	if ( !serializer.Header( "Controller", 1 ) )
		return;

	serializer( m_state );
	serializer( m_analogMode );
}

}