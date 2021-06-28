#pragma once

#include "InterruptControl.h"

#include "assert.h"
#include "bit.h"

#include <array>
#include <cstdint>

namespace PSX
{

class Timer
{
public:

	union CounterMode
	{
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

	uint32_t Read( uint32_t index ) noexcept;

	void Write( uint32_t index, uint16_t value ) noexcept;

	// update hblank and vblank for timers 0 and 1
	void UpdateBlank( bool blanked ) noexcept;

	// returns number of ticks to target, max, or infinity depending on the mode
	uint32_t GetTicksUntilIrq() const noexcept;

	// returns true if IRQ was signalled
	bool Update( uint32_t ticks ) noexcept;

private:
	bool TrySignalIrq() noexcept;

private:
	uint32_t m_counter;
	CounterMode m_mode;
	uint32_t m_target;

	bool m_irq;
	bool m_paused;
	bool m_inBlank;
};

class Timers
{
public:
	Timers( InterruptControl& interruptControl ) : m_interruptControl{ interruptControl }
	{
		Reset();
	}

	void Reset();

	Timer& operator[]( size_t index ) noexcept
	{
		return m_timers[ index ];
	}

	uint32_t Read( uint32_t offset ) noexcept;

	void Write( uint32_t offset, uint32_t value ) noexcept;

	// return current number of CPU cycles elapsed
	uint32_t GetCycles() const noexcept { return m_cycles; }

	// return true if target is reached
	bool AddCycles( uint32_t cycles ) noexcept;

	// updates timers and resets cycles
	void UpdateNow() noexcept;

	uint32_t GetCyclesUntilIrq() const noexcept;

	// set amount of cycles to run emulator until we need to check for interrupts/etc
	void SetTargetCycles( uint32_t cycles ) noexcept
	{
		dbExpects( cycles > 0 );

		// cycles should have been reset already
		dbExpects( m_cycles == 0 );
		dbExpects( m_targetCycles == 0 );

		m_targetCycles = cycles;
	}

	bool NeedsUpdate() const noexcept { return m_cycles >= m_targetCycles; }

private:
	InterruptControl& m_interruptControl;

	std::array<Timer, 3> m_timers;

	uint32_t m_cyclesDiv8Remainder;

	uint32_t m_cycles;
	uint32_t m_targetCycles; // cycle count at which an event should happen
};

}