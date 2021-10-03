#pragma once

#include "Defs.h"

#include <stdx/assert.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

namespace PSX
{

class CycleScheduler
{
public:

	using UpdateFunction = std::function<void( cycles_t )>;
	using GetCyclesFunction = std::function<cycles_t()>;

	void Register( UpdateFunction update, GetCyclesFunction getCycles )
	{
		dbExpects( !m_inUpdate ); // unsafe to register new callbacks while updating
		m_subscriptions.push_back( { std::move( update ), std::move( getCycles ) } );
	}

	void Reset() noexcept
	{
		dbExpects( !m_inUpdate ); // unsafe to reset while updating
		m_cycles = 0;
		m_cyclesUntilEvent = 0;
	}

	// add any amount of cycles to trigger one or more events
	void AddCycles( cycles_t cycles ) noexcept;

	// update cycles early (typically called before accessing registers that could alter the result)
	void UpdateEarly() noexcept
	{
		if ( m_cycles > 0 )
		{
			UpdateCycles( m_cycles );
			m_cycles = 0;
		}
	}
	
	// calculates cycles until next event
	void ScheduleNextUpdate() noexcept;

	cycles_t GetCycles() const noexcept
	{
		dbExpects( !m_inUpdate ); // unsafe to get cycles while updating
		return m_cycles;
	}

	cycles_t GetCyclesUntilEvent() const noexcept
	{
		dbExpects( !m_inUpdate ); // unsafe to get cycles while updating
		return m_cyclesUntilEvent;
	}

	bool IsUpdating() const noexcept { return m_inUpdate; }

private:
	void UpdateCycles( cycles_t cycles ) noexcept;

private:
	struct Subscription
	{
		UpdateFunction update;
		GetCyclesFunction getCycles;
	};

	std::vector<Subscription> m_subscriptions;

	cycles_t m_cycles = 0;
	cycles_t m_cyclesUntilEvent = 0;

	bool m_inUpdate = false;
};

}