#include "CycleScheduler.h"

#include <stdx/assert.h>

namespace PSX
{

void CycleScheduler::AddCycles( uint32_t cycles ) noexcept
{
	dbExpects( !m_inUpdate ); // unsafe to add cycles while updating

	m_cycles += cycles;
	while ( m_cycles >= m_cyclesUntilEvent )
	{
		if ( m_cyclesUntilEvent > 0 )
		{
			UpdateCycles( m_cyclesUntilEvent );
			m_cycles -= m_cyclesUntilEvent;
		}

		ScheduleNextUpdate();
	}
}

void CycleScheduler::UpdateCycles( uint32_t cycles ) noexcept
{
	dbExpects( !m_inUpdate ); // cannot recursively call UpdateCycles()
	dbExpects( cycles > 0 );
	dbExpects( cycles <= m_cyclesUntilEvent );

	m_inUpdate = true;

	for ( auto& subscription : m_subscriptions )
		subscription.update( cycles );

	m_inUpdate = false;
}

void CycleScheduler::ScheduleNextUpdate() noexcept
{
	dbExpects( !m_inUpdate ); // unsafe to schedule update while updating

	m_cyclesUntilEvent = std::numeric_limits<uint32_t>::max();
	for ( const auto& subscription : m_subscriptions )
		m_cyclesUntilEvent = ( std::min )( m_cyclesUntilEvent, subscription.getCycles() );
}

}