#pragma once

#include "InterruptControl.h"

#include "assert.h"
#include "bit.h"

#include <array>
#include <cstdint>

namespace PSX
{

constexpr float ClockSpeed = 44100 * 0x300; // Hz

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
	}

	void ResetCounter() noexcept
	{
		m_counter = 0;
	}

	bool GetSyncEnable() const noexcept { return m_mode.syncEnable; }
	uint32_t GetSyncMode() const noexcept { return m_mode.syncMode; }
	uint32_t GetClockSource() const noexcept { return m_mode.clockSource; }

	// pause timer for certain sync modes
	void SetPaused( bool pause ) noexcept { m_paused = pause; }

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
	Timers( InterruptControl& interruptControl ) : m_interruptControl{ interruptControl } {}

	void Reset()
	{
		for ( auto& timer : m_timers )
			timer.Reset();
	}

	Timer& GetTimer( size_t index ) noexcept
	{
		return m_timers[ index ];
	}

	uint32_t Read( uint32_t offset ) noexcept;

	void Write( uint32_t offset, uint32_t value ) noexcept;

	void SetGpuState( float refreshRate, uint32_t scanlines, uint32_t horizontalResolution ) noexcept;

	void Update() noexcept;

	bool GetEvenOdd( bool interlaced ) const noexcept
	{
		return m_vblank ? false : ( interlaced ? m_oddFrame : m_currentScanline % 2 == 1 );
	}

	void Update( uint32_t cycles ) noexcept
	{
		m_cycles += cycles;
		if ( m_cycles >= m_cyclesUntilNextEvent )
			Update();
	}

private:
	float GetCyclesPerFrame() const noexcept
	{
		ClockSpeed / m_refreshRate;
	}

	float GetCyclesPerScanline() const noexcept
	{
		return GetCyclesPerFrame() / m_scanlines;
	}

	float GetDotsPerCycle() const noexcept
	{
		return m_horizontalResolution / 2560.0f;
	}

	float GetDotsPerScanline() const noexcept
	{
		return GetDotsPerCycle() * GetCyclesPerScanline();
	}

	void UpdateCyclesUntilNextEvent() noexcept;

private:
	InterruptControl& m_interruptControl;

	std::array<Timer, 3> m_timers;

	uint32_t m_cycles;
	uint32_t m_cyclesUntilNextIRQ;
	uint32_t m_cyclesDiv8Remainder;
};

}