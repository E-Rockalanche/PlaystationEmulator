#pragma once

#include <stdx/assert.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <functional>

namespace PSX
{

using cycles_t = int32_t;

using EventUpdateCallback = std::function<void( cycles_t )>;

class Event
{
	friend class EventManager;

public:
	// Destroy event and remove from manager
	~Event();

	// Call update callback early with current accumulated cycles
	void UpdateEarly();

	// Schedule event to occur some cycles in the future
	void Schedule( cycles_t cyclesFromNow, bool resetPendingCycles = true );

	// Cancel/disable event and reset pending cycles
	void Cancel();

	// Check if event is currently enabled
	bool IsEnabled() const noexcept { return m_enabled; }

	// Get remaining cycles until event triggers (will be negative if event is late)
	cycles_t GetRemainingCycles() const noexcept
	{
		dbExpects( m_enabled );
		return m_cyclesUntilEvent - m_pendingCycles;
	}

	const std::string& GetName() const noexcept { return m_name; }

private:
	Event( EventManager& manager, std::string name, EventUpdateCallback onUpdate )
		: m_manager{ manager }
		, m_name{ std::move( name ) }
		, m_onUpdate{ std::move( onUpdate ) }
	{}

	// Adds pending cycles, but doesn't invoke update callbacks
	void AddPendingCycles( cycles_t cycles )
	{
		m_pendingCycles += cycles;
	}

	void TriggerEvent();

	// invoke the update callback with the given amount of cycles
	void Update( cycles_t cycles );

private:
	EventManager& m_manager;
	std::string m_name;
	EventUpdateCallback m_onUpdate;
	cycles_t m_cyclesUntilEvent = 0;
	cycles_t m_pendingCycles = 0;
	bool m_enabled = false;
};

class EventManager
{
	friend class Event;

public:
	Event* CreateEvent( std::string name, EventUpdateCallback onUpdate );

	Event* FindEvent( std::string_view name );
	
	// update pending cycles for next event
	void AddCycles( cycles_t cycles )
	{
		m_pendingCycles += cycles;
		while ( m_pendingCycles >= m_cyclesUntilNextEvent )
			UpdateNextEvent();
	}

	cycles_t GetPendingCycles() const noexcept { return m_pendingCycles; }

private:
	// add pending cycles to events and update next event
	void UpdateNextEvent();

	void ScheduleNextEvent();

	void RemoveEvent( const Event* event );

private:
	cycles_t m_cyclesUntilNextEvent = 0;
	cycles_t m_pendingCycles = 0;
	Event* m_nextEvent = nullptr;

	std::vector<Event*> m_events;
};

}