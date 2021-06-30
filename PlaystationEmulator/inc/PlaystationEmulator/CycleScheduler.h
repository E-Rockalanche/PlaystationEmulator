#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

namespace PSX
{

constexpr uint32_t InfiniteCycles = std::numeric_limits<uint32_t>::max();

class CycleScheduler
{
public:

	using UpdateFunction = std::function<void( uint32_t )>;
	using GetCyclesFunction = std::function<uint32_t()>;

	CycleScheduler()
	{
		Reset();
	}

	void Register( UpdateFunction update, GetCyclesFunction getCycles )
	{
		m_subscriptions.push_back( { std::move( update ), std::move( getCycles ) } );
	}

	void Reset() noexcept
	{
		m_cycles = 0;
		m_cyclesUntilEvent = 0;
	}

	void AddCycles( uint32_t cycles ) noexcept
	{
		m_cycles += cycles;
		if ( m_cycles >= m_cyclesUntilEvent )
			UpdateNow();
	}

	void UpdateNow() noexcept;

	uint32_t GetCycles() const noexcept { return m_cycles; }
	uint32_t GetCyclesUntilEvent() const noexcept { return m_cyclesUntilEvent; }

private:
	struct Subscription
	{
		UpdateFunction update;
		GetCyclesFunction getCycles;
	};

	std::vector<Subscription> m_subscriptions;

	uint32_t m_cycles;
	uint32_t m_cyclesUntilEvent;
};

}