#pragma once

#include <stdx/assert.h>

#include <cstdint>

namespace PSX
{

enum class Interrupt : uint32_t
{
	VBlank = 1,
	Gpu = 1 << 1,
	CDRom = 1 << 2,
	Dma = 1 << 3,
	Timer0 = 1 << 4,
	Timer1 = 1 << 5,
	Timer2 = 1 << 6,
	ControllerAndMemoryCard = 1 << 7,
	Sio = 1 << 8,
	Spu = 1 << 9,
	ControllerLightpen = 1 << 10
};

class InterruptControl
{
public:

	static constexpr uint32_t WriteMask = 0xffff07ffu;

	void Reset()
	{
		m_status = 0;
		m_mask = 0;
	}

	void SetInterrupt( Interrupt interrupt ) noexcept
	{
		dbLogDebug( "InterruptControl::SetInterrupt() -- [%X]", static_cast<uint32_t>( interrupt ) );
		m_status |= static_cast<uint32_t>( interrupt );
	}

	bool PendingInterrupt() const noexcept
	{
		return ( m_status & m_mask ) != 0;
	}

	uint32_t Read( uint32_t index ) const noexcept
	{
		dbExpects( index < 2 );
		switch ( index )
		{
			case 0:
				dbLogDebug( "InterruptControl::Read -- interrupt status [%X]", m_status );
				return m_status;

			case 1:
				dbLogDebug( "InterruptControl::Read -- interrupt mask [%X]", m_mask );
				return m_mask;

			default:
				dbBreak();
				return 0;
		}
	}

	void Write( uint32_t index, uint32_t value ) noexcept
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

private:
	uint32_t m_status = 0;
	uint32_t m_mask = 0;
};

}