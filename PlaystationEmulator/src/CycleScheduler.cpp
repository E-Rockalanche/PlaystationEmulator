#include "CycleScheduler.h"

#include <stdx/assert.h>

namespace PSX
{

void CycleScheduler::AddCycles( uint32_t cycles ) noexcept
{
	m_cycles += cycles;
	while ( m_cycles >= m_cyclesUntilEvent )
	{
		if ( m_cyclesUntilEvent > 0 )
		{
			UpdateSubscriberCycles( m_cyclesUntilEvent );
			m_cycles -= m_cyclesUntilEvent;
		}

		ScheduleNextSubscriberUpdate();
	}
}

void CycleScheduler::UpdateEarly() noexcept
{
	if ( m_cycles > 0 )
	{
		UpdateSubscriberCycles( m_cycles );
		m_cycles = 0;
	}
}

void CycleScheduler::UpdateSubscriberCycles( uint32_t cycles ) noexcept
{
	dbExpects( !m_inUpdate ); // update callback should not call the cycle scheduler
	dbExpects( cycles > 0 ); // can't update 0 cycles
	dbExpects( cycles <= m_cyclesUntilEvent ); // can't update more cycles than what's expected

	m_inUpdate = true;

	for ( auto& subscription : m_subscriptions )
		subscription.update( cycles );

	m_inUpdate = false;
}

void CycleScheduler::ScheduleNextSubscriberUpdate() noexcept
{
	dbExpects( !m_inUpdate ); // subscriber callback should not call the cycle scheduler

	m_cyclesUntilEvent = std::numeric_limits<uint32_t>::max();
	for ( const auto& subscription : m_subscriptions )
		m_cyclesUntilEvent = ( std::min )( m_cyclesUntilEvent, subscription.getCycles() );

	dbEnsures( m_cyclesUntilEvent > 0 ); // nothing should schedule for 0 cycles
}

}