#include "MemoryControl.h"

namespace PSX
{

void MemoryControl::Write( uint32_t index, uint32_t value ) noexcept
{
	switch ( index )
	{
		case Register::Expansion1BaseAddress:
		case Register::Expansion2BaseAddress:
			dbLogDebug( "MemoryControl::Write() -- set expansion N base address [%X]", value );
			m_registers[ index ] = 0x1f000000 | ( value & 0x00ffffff );
			break;

		case Register::Expansion1DelaySize:
		case Register::Expansion3DelaySize:
		case Register::BiosRomDelaySize:
		case Register::SpuDelaySize:
		case Register::CDRomDelaySize:
		case Register::Expansion2DelaySize:
			dbLogDebug( "MemoryControl::Write() -- set X delay size [%X]", value );
			m_registers[ index ] = value & 0xabffffff;
			break;

		case Register::CommonDelay:
			dbLogDebug( "MemoryControl::Write() -- set common delay [%X]", value );
			m_registers[ Register::CommonDelay ] = value;
			break;

		default:
			dbBreak();
			break;
	}
}

}