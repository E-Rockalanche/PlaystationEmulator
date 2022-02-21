#include "InterruptControl.h"

#include "SaveState.h"

namespace PSX
{

uint32_t InterruptControl::Read( uint32_t index ) const noexcept
{
	dbExpects( index < 2 );
	switch ( index )
	{
		case 0:
			return m_status;

		case 1:
			return m_mask;

		default:
			dbBreak();
			return 0;
	}
}

void InterruptControl::Write( uint32_t index, uint32_t value ) noexcept
{
	dbExpects( index < 2 );
	switch ( index )
	{
		case 0:
			dbLogDebug( "InterruptControl::Write -- acknowledge IRQs [%X]", value );
			m_status &= value & WriteMask;
			break;

		case 1:
			dbLogDebug( "InterruptControl::Write -- interrupt mask [%X]", value );
			m_mask = value & WriteMask;
			break;
	}
}

void InterruptControl::Serialize( SaveStateSerializer& serializer )
{
	if ( !serializer.Header( "InterruptControl", 1 ) )
		return;

	serializer( m_status );
	serializer( m_mask );
}

}