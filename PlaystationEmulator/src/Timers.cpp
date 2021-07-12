#include "Timers.h"

namespace PSX
{

uint32_t Timer::Read( uint32_t index ) noexcept
{
	dbExpects( index < 3 );
	switch ( index )
	{
		case 0:
			dbLog( "Timer::Read() -- counter" );
			return m_counter;

		case 1:
		{
			dbLog( "Timer::Read() -- mode" );
			const uint32_t value = m_mode.value;
			m_mode.reachedTarget = false;
			m_mode.reachedMax = false;
			return value;
		}

		case 2:
			dbLog( "Timer::Read() -- target" );
			return m_target;

		default:
			dbBreak();
			return 0;
	}
}

void Timer::Write( uint32_t index, uint16_t value ) noexcept
{
	dbExpects( index < 3 );
	switch ( index )
	{
		case 0:
			dbLog( "Timer::Write() -- counter value [%X]", value );
			m_counter = value;
			break;

		case 1:
		{
			dbLog( "Timer::Write() -- mode [%X]", value );
			static constexpr uint32_t WriteMask = 0x03ff;
			stdx::masked_set<uint32_t>( m_mode.value, WriteMask, value );

			// reset IRQ on write
			m_mode.noInterruptRequest = true;
			m_irq = false;

			// reset counter on write
			m_counter = 0;

			if ( !m_mode.syncEnable )
				m_paused = false;

			break;
		}

		case 2:
			dbLog( "Timer::Write() -- target [%X]", value );
			m_target = value;
			break;
	}
}

void Timer::UpdateBlank( bool blanked ) noexcept
{
	dbExpects( m_mode.syncEnable );

	if ( m_inBlank == blanked )
		return;

	m_inBlank = blanked;

	switch ( GetSyncMode() )
	{
		case 0:
			m_paused = blanked;
			break;

		case 1:
			if ( blanked )
				m_counter = 0;
			break;

		case 2:
			if ( blanked )
				m_counter = 0;
			m_paused = !blanked;
			break;

		case 3:
			m_paused = !blanked;
			if ( blanked )
				m_mode.syncEnable = false;
			break;
	}
}

uint32_t Timer::GetTicksUntilIrq() const noexcept
{
	dbExpects( m_counter <= 0xffff );

	if ( !m_paused )
	{
		if ( m_mode.irqOnTarget && m_counter < m_target )
			return m_target - m_counter;

		if ( m_mode.irqOnMax )
			return 0xffffu - m_counter;
	}

	return std::numeric_limits<uint32_t>::max();
}

bool Timer::Update( uint32_t ticks ) noexcept
{
	if ( m_mode.syncEnable && m_paused )
		return false;

	const auto oldCounter = m_counter;
	m_counter += ticks;

	bool irq = false;

	if ( m_counter >= m_target && ( oldCounter < m_target || m_target == 0 ) )
	{
		m_mode.reachedTarget = true;
		irq |= m_mode.irqOnTarget;

		if ( m_mode.resetCounter )
		{
			// TODO: counter actually resets when it exceeds target
			m_counter = ( m_target > 0 ) ? ( m_counter % m_target ) : 0;
		}
	}

	if ( m_counter >= 0xffff )
	{
		m_mode.reachedMax = true;
		irq |= m_mode.irqOnMax;
		m_counter %= 0xffff; // TODO: counter actually resets when it overflows
	}

	if ( irq )
	{
		if ( m_mode.irqToggle )
		{
			m_mode.noInterruptRequest ^= true;

			if ( m_mode.noInterruptRequest == false )
				return TrySignalIrq();
		}
		else
		{
			// pulse
			m_mode.noInterruptRequest = true;
			return TrySignalIrq();
		}
	}

	return false;
}

bool Timer::TrySignalIrq() noexcept
{
	if ( !m_irq || m_mode.irqRepeat )
	{
		m_irq = true;
		return true;
	}

	return false;
}

void Timer::PauseAtTarget() noexcept
{
	if ( m_mode.irqOnTarget )
		m_counter = m_target;
	else if ( m_mode.irqOnMax )
		m_counter = 0xffff;

	m_paused = true;
}

void Timers::Reset()
{
	for ( auto& timer : m_timers )
		timer.Reset();

	m_cyclesDiv8Remainder = 0;
}

uint32_t Timers::Read( uint32_t offset ) noexcept
{
	const uint32_t timerIndex = offset >> 4;
	dbAssert( timerIndex < 3 );

	const uint32_t registerIndex = ( offset & 0xf ) / 4;
	dbAssert( registerIndex < 3 );

	if ( registerIndex < 3 )
	{
		m_cycleScheduler.UpdateSubscriberCycles();
		m_cycleScheduler.ScheduleNextSubscriberUpdate();
		return m_timers[ timerIndex ].Read( registerIndex );
	}
	else
	{
		dbLogError( "Timers::Read() -- read from 4th register" );
		return 0;
	}
}

void Timers::Write( uint32_t offset, uint32_t value ) noexcept
{
	const uint32_t timerIndex = offset >> 4;
	dbAssert( timerIndex < 3 );

	const uint32_t registerIndex = ( offset & 0xf ) / 4;

	if ( registerIndex < 3 )
	{
		m_cycleScheduler.UpdateSubscriberCycles();
		m_timers[ timerIndex ].Write( registerIndex, static_cast<uint16_t>( value ) );
		m_cycleScheduler.ScheduleNextSubscriberUpdate();
	}
	else
	{
		dbLogError( "Timers::Write() -- write to 4th register" );
	}
}

void Timers::AddCycles( uint32_t cycles ) noexcept
{
	dbExpects( cycles > 0 );

	// timer0
	{
		auto& dotTimer = m_timers[ 0 ];
		if ( dotTimer.GetClockSource() % 2 == 0 )
			if ( dotTimer.Update( cycles ) )
				m_interruptControl.SetInterrupt( Interrupt::Timer0 );
	}

	// timer1
	{
		auto& hblankTimer = m_timers[ 1 ];
		if ( hblankTimer.GetClockSource() % 2 == 0 )
			if ( hblankTimer.Update( cycles ) )
				m_interruptControl.SetInterrupt( Interrupt::Timer1 );
	}

	// timer2
	{
		auto& cpuTimer = m_timers[ 2 ];
		const bool useDiv8 = cpuTimer.GetClockSource() & 0x2;
		uint32_t ticks;
		if ( useDiv8 )
		{
			ticks = ( cycles + m_cyclesDiv8Remainder ) / 8;
			m_cyclesDiv8Remainder = ( cycles + m_cyclesDiv8Remainder ) % 8;
		}
		else
		{
			ticks = cycles;
		}

		if ( cpuTimer.Update( ticks ) )
		{
			m_interruptControl.SetInterrupt( Interrupt::Timer2 );

			const auto syncMode = cpuTimer.GetSyncMode();
			if ( syncMode == 0 || syncMode == 3 )
			{
				// stop at current value with no restart
				cpuTimer.PauseAtTarget();
			}
			else
			{
				// switch to free run
				cpuTimer.SetSyncEnable( false );
			}
		}
	}
}

uint32_t Timers::GetCyclesUntilIrq() const noexcept
{
	uint32_t cycles = std::numeric_limits<uint32_t>::max();

	auto& dotTimer = m_timers[ 0 ];
	if ( dotTimer.GetClockSource() % 2 == 0 )
	{
		cycles = std::min( cycles, dotTimer.GetTicksUntilIrq() );
	}

	auto& hblankTimer = m_timers[ 0 ];
	if ( hblankTimer.GetClockSource() % 2 == 0 )
	{
		cycles = std::min( cycles, hblankTimer.GetTicksUntilIrq() );
	}

	auto& cpuTimer = m_timers[ 2 ];
	if ( cpuTimer.GetClockSource() & 0x2 )
	{
		cycles = std::min( cycles, cpuTimer.GetTicksUntilIrq() * 8 - m_cyclesDiv8Remainder );
	}
	else
	{
		cycles = std::min( cycles, cpuTimer.GetTicksUntilIrq() );
	}

	dbAssert( cycles > 0 );
	return cycles;
}

}