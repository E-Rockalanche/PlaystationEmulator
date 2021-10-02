#pragma once

#include "Defs.h"

#include <stdx/assert.h>
#include <stdx/bit.h>

#include <array>
#include <cstdint>

namespace PSX
{

class Timer
{
public:

	union CounterMode
	{
		CounterMode() : value{ 0 } {}

		struct
		{
			uint32_t syncEnable : 1; // 0=free run, 1=sync mode

			/*
			Synchronization Modes for Counter 0:
				0 = Pause counter during Hblank(s)
				1 = Reset counter to 0000h at Hblank(s)
				2 = Reset counter to 0000h at Hblank(s) and pause outside of Hblank
				3 = Pause until Hblank occurs once, then switch to Free Run
			Synchronization Modes for Counter 1:
				Same as above, but using Vblank instead of Hblank
			Synchronization Modes for Counter 2:
				0 or 3 = Stop counter at current value (forever, no h/v-blank start)
				1 or 2 = Free Run (same as when Synchronization Disabled)
			*/
			uint32_t syncMode : 2;
			uint32_t resetCounter : 1; // 0=after counter=0xffff, 1=after counter=target
			uint32_t irqOnTarget : 1;
			uint32_t irqOnMax : 1;
			uint32_t irqRepeat : 1; // 0=once, 1=repeat
			uint32_t irqToggle : 1; // 0=pulse interrupt request, 1=toggle interrupt request

			/*
			Counter 0:  0 or 2 = System Clock,  1 or 3 = Dotclock
			Counter 1:  0 or 2 = System Clock,  1 or 3 = Hblank
			Counter 2:  0 or 1 = System Clock,  2 or 3 = System Clock/8
			*/
			uint32_t clockSource : 2;
			uint32_t noInterruptRequest : 1; // 0=yes, 1=no (set after writing) (R)
			uint32_t reachedTarget : 1; // reset after reading (R)
			uint32_t reachedMax : 1; // reset after reading (R)
			uint32_t : 19;
		};
		uint32_t value;
	};
	static_assert( sizeof( CounterMode ) == 4 );

	Timer( uint32_t index ) noexcept : m_index{ index } {}

	void Reset()
	{
		m_counter = 0;
		m_mode.value = 0;
		m_mode.noInterruptRequest = true;
		m_target = 0;
		m_irq = false;
		m_paused = false;
		m_inBlank = false;
	}

	void ResetCounter() noexcept
	{
		m_counter = 0;
	}

	void SetSyncEnable( bool enable ) noexcept { m_mode.syncEnable = enable; }
	bool GetSyncEnable() const noexcept { return m_mode.syncEnable; }

	uint32_t GetSyncMode() const noexcept { return m_mode.syncMode; }
	uint32_t GetClockSource() const noexcept { return m_mode.clockSource; }

	bool GetPaused() const noexcept { return m_paused; }
	void PauseAtTarget() noexcept; // timer2 only

	bool CanTriggerIrq() const noexcept
	{
		return !m_paused && ( m_mode.irqOnTarget || m_mode.irqOnMax );
	}

	uint32_t Read( uint32_t index ) noexcept;

	void Write( uint32_t index, uint16_t value ) noexcept;

	// update hblank and vblank for timers 0 and 1
	void UpdateBlank( bool blanked ) noexcept;

	// returns number of ticks to target, max, or infinity depending on the mode
	uint32_t GetTicksUntilIrq() const noexcept;

	// returns true if IRQ was signalled
	bool Update( uint32_t ticks ) noexcept;

private:
	void UpdatePaused() noexcept;
	bool TrySignalIrq() noexcept;

private:
	uint32_t m_counter = 0;
	CounterMode m_mode;
	uint32_t m_target = 0;

	bool m_irq = false;
	bool m_paused = false;
	bool m_inBlank = false;

	uint32_t m_index = 0;
};

class Timers
{
public:
	Timers( InterruptControl& interruptControl, EventManager& eventManager );
	~Timers();

	void Reset();

	Timer& operator[]( size_t index ) noexcept
	{
		return m_timers[ index ];
	}

	uint32_t Read( uint32_t offset ) noexcept;
	void Write( uint32_t offset, uint32_t value ) noexcept;

	void AddCycles( cycles_t cycles ) noexcept;
	void ScheduleNextIrq() noexcept;

private:
	InterruptControl& m_interruptControl;
	EventHandle m_timerEvent;

	std::array<Timer, 3> m_timers{ Timer( 0 ), Timer( 1 ), Timer( 2 ) };

	uint32_t m_cyclesDiv8Remainder = 0;
};

}