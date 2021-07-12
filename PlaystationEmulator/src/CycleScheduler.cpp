#include "CycleScheduler.h"

namespace PSX
{

void CycleScheduler::AddCycles( uint32_t cycles ) noexcept
{
	m_cycles += cycles;
	while ( m_cycles >= m_cyclesUntilEvent )
	{
		UpdateSubscriberCycles( m_cyclesUntilEvent );
		m_cycles -= m_cyclesUntilEvent;

		ScheduleNextSubscriberUpdate();
	}
}

void CycleScheduler::UpdateSubscriberCycles( uint32_t cycles ) noexcept
{
	if ( cycles > 0 )
	{
		for ( auto& subscription : m_subscriptions )
			subscription.update( cycles );
	}
}

void CycleScheduler::ScheduleNextSubscriberUpdate() noexcept
{
	m_cyclesUntilEvent = std::numeric_limits<uint32_t>::max();
	for ( const auto& subscription : m_subscriptions )
		m_cyclesUntilEvent = ( std::min )( m_cyclesUntilEvent, subscription.getCycles() );
}

}