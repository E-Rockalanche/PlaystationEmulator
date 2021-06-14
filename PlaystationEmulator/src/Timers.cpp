#include "Timers.h"

namespace PSX
{

uint32_t Timer::Read( uint32_t index ) noexcept
{
	dbExpects( index < 3 );
	switch ( index )
	{
		case 0:
			dbLog( "Timer::Read -- counter" );
			return m_counter;

		case 1:
		{
			dbLog( "Timer::Read -- mode" );
			const uint32_t value = m_mode.value;
			m_mode.reachedTarget = false;
			m_mode.reachedMax = false;
			return value;
		}

		case 2:
			dbLog( "Timer::Read -- target" );
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
			dbLog( "Timer::Write -- counter value [%X]", value );
			m_counter = value;
			break;

		case 1:
			dbLog( "Timer::Write -- mode [%X]", value );
			static constexpr uint32_t WriteMask = 0x03ff;
			stdx::masked_set<uint32_t>( m_mode.value, WriteMask, value );

			// reset IRQ on write
			m_mode.noInterruptRequest = true;
			m_irq = false;

			// reset counter on write
			m_counter = 0;
			break;

		case 2:
			dbLog( "Timer::Write -- target [%X]", value );
			m_target = value;
			break;
	}
}

void Timer::UpdateBlank( bool blanked ) noexcept
{
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

	if ( m_mode.irqOnTarget && m_counter < m_target )
		return m_target - m_counter;

	if ( m_mode.irqOnMax )
		return 0xffffu - m_counter;

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

		if ( m_mode.resetCounter && m_target > 0 )
			m_counter %= m_target; // TODO: counter actually resets when it exceeds target
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

uint32_t Timers::Read( uint32_t offset ) noexcept
{
	const uint32_t timerIndex = offset >> 4;
	dbExpects( timerIndex < 3 );

	const uint32_t registerIndex = ( offset & 0xf ) / 4;
	dbExpects( registerIndex < 3 );

	if ( registerIndex < 3 )
	{
		return m_timers[ timerIndex ].Read( registerIndex );
	}
	else
	{
		dbLogError( "Timers::Read -- read from 4th register" );
		return 0;
	}
}

void Timers::Write( uint32_t offset, uint32_t value ) noexcept
{
	const uint32_t timerIndex = offset >> 4;
	dbExpects( timerIndex < 3 );

	const uint32_t registerIndex = ( offset & 0xf ) / 4;

	if ( registerIndex < 3 )
	{
		m_timers[ timerIndex ].Write( registerIndex, static_cast<uint16_t>( value ) );
	}
	else
	{
		dbLogError( "Timers::Write -- write to 4th register" );
	}
}



void Timers::UpdateCyclesUntilNextEvent() noexcept
{
	m_cyclesUntilNextEvent = std::numeric_limits<uint32_t>::max();

	{
		auto& timer0 = m_timers[ 0 ];
		if ( timer0. )
	}
}

void Timers::SetGpuState( float refreshRate, uint32_t scanlines, uint32_t horizontalResolution ) noexcept
{
	Update();

	m_refreshRate = refreshRate;
	m_scanlines = scanlines;
	m_horizontalResolution = horizontalResolution;
}

void Timers::Update() noexcept
{
	const auto dots = m_cycles * GetDotsPerCycle();
	m_currentDot += dots;
	m_hblank = m_currentDot >= m_horizontalResolution;

	const auto dotsPerScanline = GetDotsPerScanline();
	if ( m_currentDot >= dotsPerScanline )
	{
		m_currentDot -= dotsPerScanline;

		m_currentScanline = ( m_currentScanline + 1 ) % m_scanlines;

		m_displayFrame = ( m_currentScanline == 240 );
	}

	m_vblank = m_currentScanline >= 240;

	{
		auto& timer0 = m_timers[ 0 ];
		const bool useDotClock = timer0.GetClockSource() % 2;
		const uint32_t ticks = useDotClock ? static_cast<uint32_t>( m_cycles * GetDotsPerCycle() ) : m_cycles;
		const bool irq = timer0.Update( ticks );
		if ( timer0.GetSyncEnable() )
			timer0.UpdateBlank( m_hblank );

		// signal irq
	}

	{
		auto& timer1 = m_timers[ 1 ];
		const bool useHBlank = timer1.GetClockSource() % 2;
		const uint32_t ticks = useHBlank ? static_cast<uint32_t>( m_hblank ) : m_cycles;
		const bool irq = timer1.Update( ticks );
		if ( timer1.GetSyncEnable() )
			timer1.UpdateBlank( m_vblank );

		// signal irq
	}

	{
		auto& timer2 = m_timers[ 2 ];
		const bool useDiv8 = timer2.GetClockSource() & 0x2;
		uint32_t ticks;
		if ( useDiv8 )
		{
			ticks = ( m_cycles + m_cyclesDiv8Remainder ) / 8;
			m_cyclesDiv8Remainder = ( m_cycles + m_cyclesDiv8Remainder ) % 8;
		}
		else
		{
			ticks = m_cycles;
		}
		const bool irq = timer2.Update( ticks );
		if ( timer2.GetSyncEnable() )
		{
			timer2.SetPaused( timer2.GetSyncMode() % 3 == 0 );
		}
	}
	m_cycles = 0;
}

}