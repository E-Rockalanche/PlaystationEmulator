#include "Controller.h"

namespace PSX
{

uint8_t Controller::Communicate( uint8_t value )
{
	if ( value == 0x01 )
	{
		return 0xff;
	}
	else if ( value == 'B' )
	{
		// this is sequence 0
		m_sequence = 1;
		return static_cast<uint16_t>( ID ) & 0xff; // lower byte of ID, containing type and halfword count
	}
	else
	{
		switch ( m_sequence++ )
		{
			case 1:
				// TODO: handle TAP
				return 0x5a; // upper byte of ID, always 5A

			case 2:
				// TODO: handle MOT1
				return static_cast<uint8_t>( m_buttons );

			case 3:
				// TODO: handle MOT2
				return static_cast<uint8_t>( m_buttons >> 8 );

			default:
				return 0xff; // what happens if we keep reading?
		}
	}
}

}