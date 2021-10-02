#include "Timers.h"

#include "EventManager.h"
#include "InterruptControl.h"

namespace PSX
{

uint32_t Timer::Read( uint32_t index ) noexcept
{
	dbExpects( index < 3 );
	switch ( index )
	{
		case 0:
			// Timer2 counter is read a LOT
			// dbLog( "Timer%u::Read() -- counter [%X]", m_index, m_counter );
			return m_counter;

		case 1:
		{
			dbLog( "Timer%u::Read() -- mode [%X]", m_index, m_mode.value );
			const uint32_t value = m_mode.value;
			m_mode.reachedTarget = false;
			m_mode.reachedMax = false;
			return value;
		}

		case 2:
			dbLog( "Timer%u::Read() -- target [%X]", m_index, m_target );
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
			dbLog( "Timer%u::Write() -- counter [%X]", m_index, value );
			m_counter = value;
			// TODO: this can trigger an IRQ?
			break;

		case 1:
		{
			dbLog( "Timer%u::Write() -- mode [%X]", m_index, value );
			static constexpr uint32_t WriteMask = 0b1110001111111111;
			stdx::masked_set<uint32_t>( m_mode.value, WriteMask, value );

			// reset IRQ on write
			m_irq = false;

			// reset counter on write
			m_counter = 0;

			// In Toggle mode, Bit10 is set after writing to the Mode register, and becomes inverted on each IRQ
			if ( m_mode.irqToggle )
				m_mode.noInterruptRequest = true;

			UpdatePaused();

			// TODO: this can trigger an IRQ?
			break;
		}

		case 2:
			dbLog( "Timer%u::Write() -- target [%X]", m_index, value );
			m_target = value;

			// TODO: this can trigger an IRQ?
			break;
	}
}

void Timer::UpdateBlank( bool blanked ) noexcept
{
	if ( m_inBlank == blanked )
		return;

	m_inBlank = blanked;

	if ( m_mode.syncEnable && blanked )
	{
		switch ( GetSyncMode() )
		{
			case 0:								break;
			case 1:	m_counter = 0;				break;
			case 2: m_counter = 0;				break;
			case 3: m_mode.syncEnable = false;	break;
		}
	}

	UpdatePaused();
}

void Timer::UpdatePaused() noexcept
{
	if ( m_mode.syncEnable )
	{
		if ( m_index != 2 )
		{
			switch ( GetSyncMode() )
			{
				case 0:	m_paused = m_inBlank;	break;
				case 1:	m_paused = false;		break;
				case 2:
				case 3:	m_paused = !m_inBlank;	break;
			}
		}
		else
		{
			m_paused = m_paused && ( GetSyncMode() % 3 == 0 );
		}
	}
	else
	{
		m_paused = false;
	}
}

uint32_t Timer::GetTicksUntilIrq() const noexcept
{
	dbExpects( m_counter <= 0xffff );
	dbExpects( CanTriggerIrq() );

	auto minTicks = std::numeric_limits<uint32_t>::max();

	if ( m_mode.irqOnTarget )
	{
		const auto ticks = ( m_counter < m_target ) ? ( m_target - m_counter ) : ( ( 0xffff - m_counter ) + m_target );
		minTicks = std::min( minTicks, ticks );
	}

	if ( m_mode.irqOnMax )
	{
		const auto ticks = 0xffffu - m_counter;
		minTicks = std::min( minTicks, ticks );
	}

	return minTicks;
}

bool Timer::Update( uint32_t ticks ) noexcept
{
	if ( m_paused )
	{
		dbAssert( m_mode.syncEnable );
		return false;
	}

	dbAssert( ticks <= GetTicksUntilIrq() );

	const auto oldCounter = m_counter;
	m_counter += ticks;

	bool irq = false;

	if ( m_counter >= m_target && ( oldCounter < m_target || m_target == 0 ) )
	{
		m_mode.reachedTarget = true;
		irq |= m_mode.irqOnTarget;

		/*
		// TODO: does counter continue if target is 0?
		if ( m_mode.resetCounter )
			m_counter = ( m_target > 0 ) ? ( m_counter % m_target ) : 0;
		*/

		// duckstation does this
		if ( m_mode.resetCounter && m_target > 0 )
			m_counter %= m_target;
	}

	if ( m_counter >= 0xffff )
	{
		m_mode.reachedMax = true;
		irq |= m_mode.irqOnMax;
		m_counter %= 0xffffu;
	}

	if ( irq )
	{
		if ( m_mode.irqToggle )
		{
			if ( m_mode.irqRepeat || m_mode.noInterruptRequest )
			{
				m_mode.noInterruptRequest ^= true;

				if ( m_mode.noInterruptRequest == false )
					return TrySignalIrq();
			}
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
		dbLog( "Timer%u signalled IRQ", m_index );
		m_irq = true;
		return true;
	}

	return false;
}

void Timer::PauseAtTarget() noexcept
{
	dbExpects( m_mode.syncEnable );

	if ( m_mode.irqOnTarget && m_mode.reachedTarget )
	{
		m_counter = m_target;
	}
	else
	{
		dbAssert( m_mode.irqOnMax && m_mode.reachedMax );
		m_counter = 0xffff;
	}

	m_paused = true;
}

Timers::Timers( InterruptControl& interruptControl, EventManager& eventManager )
	: m_interruptControl{ interruptControl }
{
	m_timerEvent = eventManager.CreateEvent( "Timers event", [this]( cycles_t cycles ) { AddCycles( cycles ); } );
}

void Timers::Reset()
{
	for ( auto& timer : m_timers )
		timer.Reset();

	m_cyclesDiv8Remainder = 0;
}

uint32_t Timers::Read( uint32_t offset ) noexcept
{
	const uint32_t timerIndex = offset / 4;
	dbAssert( timerIndex < 3 );

	const uint32_t registerIndex = offset % 4;
	dbAssert( registerIndex < 3 );

	if ( registerIndex < 3 )
	{
		m_timerEvent->UpdateEarly();
		const auto value = m_timers[ timerIndex ].Read( registerIndex );
		ScheduleNextIrq();
		return value;
	}
	else
	{
		dbLogError( "Timers::Read() -- read from 4th register" );
		return 0;
	}
}

void Timers::Write( uint32_t offset, uint32_t value ) noexcept
{
	const uint32_t timerIndex = offset / 4;
	dbAssert( timerIndex < 3 );

	const uint32_t registerIndex = offset % 4;
	dbAssert( registerIndex < 3 );

	if ( registerIndex < 3 )
	{
		m_timerEvent->UpdateEarly();
		m_timers[ timerIndex ].Write( registerIndex, static_cast<uint16_t>( value ) );
		ScheduleNextIrq();
	}
	else
	{
		dbLogError( "Timers::Write() -- write to 4th register" );
	}
}

void Timers::AddCycles( cycles_t cycles ) noexcept
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

			if ( cpuTimer.GetSyncEnable() )
			{
				const auto syncMode = cpuTimer.GetSyncMode();
				if ( syncMode == 0 || syncMode == 3 )
				{
					// stop at current value with no restart
					cpuTimer.PauseAtTarget();
				}
			}
		}
	}

	ScheduleNextIrq();
}

void Timers::ScheduleNextIrq() noexcept
{
	cycles_t minCycles = InfiniteCycles;

	auto& timer0 = m_timers[ 0 ];
	if ( timer0.CanTriggerIrq() && timer0.GetClockSource() % 2 == 0 )
	{
		minCycles = std::min( minCycles, static_cast<cycles_t>( timer0.GetTicksUntilIrq() ) );
	}

	auto& timer1 = m_timers[ 0 ];
	if ( timer1.CanTriggerIrq() && timer1.GetClockSource() % 2 == 0 )
	{
		minCycles = std::min( minCycles, static_cast<cycles_t>( timer1.GetTicksUntilIrq() ) );
	}

	auto& timer2 = m_timers[ 2 ];
	if ( timer2.CanTriggerIrq() )
	{
		if ( timer2.GetClockSource() & 0x2 )
		{
			const auto rawCycles = timer2.GetTicksUntilIrq();
			if ( rawCycles != std::numeric_limits<uint32_t>::max() )
				minCycles = std::min( minCycles, static_cast<cycles_t>( rawCycles * 8 - m_cyclesDiv8Remainder ) );
		}
		else
		{
			minCycles = std::min( minCycles, static_cast<cycles_t>( timer2.GetTicksUntilIrq() ) );
		}
	}

	dbAssert( minCycles > 0 );

	if ( minCycles != InfiniteCycles )
		m_timerEvent->Schedule( minCycles );
}

}