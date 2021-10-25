#pragma once

#include "Defs.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace PSX
{

using EventHandle = std::unique_ptr<Event>;

using EventUpdateCallback = std::function<void( cycles_t )>;

class EventManager;

class Event
{
	friend class EventManager;

public:
	~Event();

	// Call update callback early with current accumulated cycles
	void UpdateEarly();

	// Schedule event to occur in the future
	void Schedule( cycles_t cyclesFromNow );

	// Cancel/disable event and reset pending cycles
	void Cancel();

	// Check if event is currently running
	bool IsActive() const noexcept { return m_active; }

	// Get pending cycles to apply
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
		if ( m_active )
			m_pendingCycles += cycles;
	}

	// Update event with given cycles. Called by EventManager
	void Update( cycles_t cycles );
	void Update() { Update( m_cyclesUntilEvent ); }

	// Get remaining cycles until event triggers (will be negative if event is late) (does not include pending cycles in event manager)
	cycles_t GetLocalRemainingCycles() const noexcept
	{
		dbExpects( ( m_cyclesUntilEvent >= m_pendingCycles ) == ( m_cyclesUntilEvent - m_pendingCycles >= 0 ) );
		return m_cyclesUntilEvent - m_pendingCycles;
	}

private:
	EventManager& m_manager;
	std::string m_name;
	EventUpdateCallback m_onUpdate;
	cycles_t m_cyclesUntilEvent = 0;
	cycles_t m_pendingCycles = 0;
	bool m_active = false;
};

class EventManager
{
	friend class Event;

public:
	EventManager() = default;
	~EventManager();

	void Reset();

	EventHandle CreateEvent( std::string name, EventUpdateCallback onUpdate );

	Event* FindEvent( std::string_view name );
	
	// update pending cycles for next event
	void AddCycles( cycles_t cycles )
	{
		dbExpects( cycles > 0 );
		m_pendingCycles += cycles;
		m_totalFrameCycles += cycles;
		while ( m_pendingCycles >= m_cyclesUntilNextEvent )
			UpdateNextEvent();
	}

	cycles_t GetPendingCycles() const noexcept { return m_pendingCycles; }

	cycles_t GetTotalFrameCycles() const noexcept { return m_totalFrameCycles; }
	void ResetTotalFrameCycles() noexcept { m_totalFrameCycles = 0; }

private:
	void UpdateNextEvent();

	void ScheduleNextEvent();

	void RemoveEvent( Event* event );

private:
	// cached cycles for nest event
	cycles_t m_cyclesUntilNextEvent = 0;
	cycles_t m_pendingCycles = 0;
	cycles_t m_totalFrameCycles = 0;

	std::vector<Event*> m_events;
	Event* m_nextEvent = nullptr;
};

}