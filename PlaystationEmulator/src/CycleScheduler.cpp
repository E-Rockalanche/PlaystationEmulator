#include "CycleScheduler.h"

namespace PSX
{


void CycleScheduler::UpdateSubscriberCycles() noexcept
{
	if ( m_cycles > 0 )
	{
		for ( auto& subscription : m_subscriptions )
			subscription.update( m_cycles );
	}
	m_cycles = 0;
}

void CycleScheduler::ScheduleNextSubscriberUpdate() noexcept
{
	m_cyclesUntilEvent = std::numeric_limits<uint32_t>::max();
	for ( const auto& subscription : m_subscriptions )
		m_cyclesUntilEvent = ( std::min )( m_cyclesUntilEvent, subscription.getCycles() );
}

}