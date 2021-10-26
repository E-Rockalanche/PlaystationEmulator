#include "Timers.h"

#include "EventManager.h"
#include "InterruptControl.h"
#include "GPU.h"

namespace PSX
{

void Timer::Reset()
{
	m_counter = 0;
	m_mode.value = 0;
	m_mode.noInterruptRequest = true;
	m_target = 0;
	m_irq = false;
	m_paused = false;
	m_inBlank = false;
	m_useSystemClock = true;
}

uint32_t Timer::ReadMode() noexcept
{
	const uint32_t value = m_mode.value;
	m_mode.reachedTarget = false;
	m_mode.reachedMax = false;
	return value;
}

void Timer::SetMode( uint32_t mode ) noexcept
{
	static constexpr uint32_t WriteMask = 0b1110001111111111;
	stdx::masked_set<uint32_t>( m_mode.value, WriteMask, mode );

	// reset IRQ on write
	m_irq = false;

	// reset counter on write
	m_counter = 0;

	// In Toggle mode, Bit10 is set after writing to the Mode register, and becomes inverted on each IRQ
	if ( m_mode.irqToggle )
		m_mode.noInterruptRequest = true;

	UpdatePaused();

	// cached result
	m_useSystemClock = ( GetClockSource() & ( m_index == 2 ? 0x02 : 0x01 ) ) == 0;

	// TODO: this can trigger an IRQ?
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

	uint32_t minTicks = InfiniteCycles;

	if ( !m_paused )
	{
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
	}

	dbAssert( minTicks > 0 );
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
		dbLogDebug( "Timer%u signalled IRQ", m_index );
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
	m_timerEvent = eventManager.CreateEvent( "Timer event", [this]( cycles_t cycles ) { AddCycles( cycles ); } );
}

Timers::~Timers() = default;

void Timers::Reset()
{
	for ( auto& timer : m_timers )
		timer.Reset();

	m_cyclesDiv8Remainder = 0;

	m_timerEvent->Schedule( MaxScheduleCycles );
}

void Timers::UpdateEventsEarly( uint32_t timerIndex )
{
	if ( timerIndex < 2 )
	{
		auto& timer = m_timers[ timerIndex ];
		if ( timer.GetSyncEnable() || !timer.IsUsingSystemClock() )
			m_gpu->UpdateClockEventEarly();
	}

	m_timerEvent->UpdateEarly();
}

uint32_t Timers::Read( uint32_t offset ) noexcept
{
	const uint32_t timerIndex = offset / 4;
	if ( timerIndex >= 3 )
	{
		dbLogWarning( "Timers::Read -- invalid timer" );
		return 0xffffffffu;
	}

	auto& timer = m_timers[ timerIndex ];

	switch ( static_cast<TimerRegister>( offset % 4 ) )
	{
		case TimerRegister::Counter:
			UpdateEventsEarly( timerIndex );
			return timer.GetCounter();

		case TimerRegister::Mode:
			UpdateEventsEarly( timerIndex );
			return timer.ReadMode();

		case TimerRegister::Target:
			return timer.GetTarget();

		default:
			dbLogWarning( "Timers::Read -- invalid timer register" );
			return 0xffffffffu;
	}
}

void Timers::Write( uint32_t offset, uint32_t value ) noexcept
{
	const uint32_t timerIndex = offset / 4;
	if ( timerIndex >= 3 )
	{
		dbLogWarning( "Timers::Write -- invalid timer index" );
		return;
	}

	auto& timer = m_timers[ timerIndex ];

	UpdateEventsEarly( timerIndex );

	switch ( static_cast<TimerRegister>( offset % 4 ) )
	{
		case TimerRegister::Counter:
		{
			timer.SetCounter( value );
			break;
		}

		case TimerRegister::Mode:
		{
			timer.SetMode( value );
			break;
		}

		case TimerRegister::Target:
		{
			timer.SetTarget( value );
			break;
		}

		default:
		{
			dbLogWarning( "Timers::Write -- invalid timer register" );
			break;
		}
	}

	ScheduleNextIrq();

	if ( timerIndex < 2 && !timer.IsUsingSystemClock() )
		m_gpu->ScheduleNextEvent();
}

void Timers::AddCycles( cycles_t cycles ) noexcept
{
	dbExpects( cycles > 0 );

	// timer0
	{
		auto& timer0 = m_timers[ 0 ];
		if ( timer0.IsUsingSystemClock() && timer0.Update( cycles ) )
			m_interruptControl.SetInterrupt( Interrupt::Timer0 );
	}

	// timer1
	{
		auto& timer1 = m_timers[ 1 ];
		if ( timer1.IsUsingSystemClock() && timer1.Update( cycles ) )
			m_interruptControl.SetInterrupt( Interrupt::Timer1 );
	}

	// timer2
	{
		auto& timer2 = m_timers[ 2 ];
		uint32_t ticks;
		if ( timer2.IsUsingSystemClock() )
		{
			ticks = cycles;
		}
		else
		{
			ticks = ( cycles + m_cyclesDiv8Remainder ) / 8;
			m_cyclesDiv8Remainder = ( cycles + m_cyclesDiv8Remainder ) % 8;
		}

		if ( timer2.Update( ticks ) )
		{
			m_interruptControl.SetInterrupt( Interrupt::Timer2 );

			if ( timer2.GetSyncEnable() )
			{
				const auto syncMode = timer2.GetSyncMode();
				if ( syncMode == 0 || syncMode == 3 )
				{
					// stop at current value with no restart
					timer2.PauseAtTarget();
				}
			}
		}
	}

	ScheduleNextIrq();
}

void Timers::ScheduleNextIrq() noexcept
{
	cycles_t minCycles = MaxScheduleCycles;

	// timer0
	{
		auto& timer0 = m_timers[ 0 ];
		if ( timer0.IsUsingSystemClock() )
		{
			minCycles = std::min( minCycles, static_cast<cycles_t>( timer0.GetTicksUntilIrq() ) );
		}
	}

	// timer1
	{
		auto& timer1 = m_timers[ 1 ];
		if ( timer1.IsUsingSystemClock() )
		{
			minCycles = std::min( minCycles, static_cast<cycles_t>( timer1.GetTicksUntilIrq() ) );
		}
	}

	// timer2
	{
		auto& timer2 = m_timers[ 2 ];
		if ( timer2.IsUsingSystemClock() )
		{
			minCycles = std::min( minCycles, static_cast<cycles_t>( timer2.GetTicksUntilIrq() ) );
		}
		else
		{
			const auto ticksDiv8 = timer2.GetTicksUntilIrq();
			if ( ticksDiv8 != InfiniteCycles )
				minCycles = std::min( minCycles, static_cast<cycles_t>( ticksDiv8 * 8 - m_cyclesDiv8Remainder ) );
		}
	}

	// timers need to be scheduled even if an IRQ won't happen
	m_timerEvent->Schedule( minCycles );
}

}