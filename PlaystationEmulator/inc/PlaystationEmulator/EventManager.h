#pragma once

#include "Defs.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <functional>

namespace PSX
{

using EventUpdateCallback = std::function<void( cycles_t )>;

class EventManager;

class Event
{
	friend class EventManager;

public:
	// Call update callback early with current accumulated cycles
	void UpdateEarly();

	// Schedule event to occur some cycles in the future. Should reset pending cycles if called outside update callback
	void Schedule( cycles_t cyclesFromNow, bool resetPendingCycles = false );

	// Cancel/disable event and reset pending cycles
	void Cancel();

	// Check if event is currently enabled
	bool IsEnabled() const noexcept { return m_enabled; }

	// Get pending cycles to apply (includes pending cycles in event manager)
	cycles_t GetPendingCycles() const noexcept;

	// Get remaining cycles until event triggers (will be negative if event is late)
	cycles_t GetRemainingCycles() const noexcept
	{
		return m_cyclesUntilEvent - GetPendingCycles();
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
	EventManager() = default;
	~EventManager();

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

	void Reset();

private:
	// add pending cycles to events and update next event
	void UpdateNextEvent();

	void ScheduleNextEvent();

private:
	cycles_t m_cyclesUntilNextEvent = 0;
	cycles_t m_pendingCycles = 0;
	Event* m_nextEvent = nullptr;

	std::vector<Event*> m_events;
};

}