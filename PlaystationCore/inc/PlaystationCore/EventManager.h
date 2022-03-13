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
	Event( const Event& ) = delete;
	Event( Event&& ) = delete;

	~Event();

	Event& operator=( const Event& ) = delete;
	Event& operator=( Event&& ) = delete;

	// Reset state without rescheduling
	void Reset();

	// Call update callback early with current accumulated cycles
	void UpdateEarly();

	// Schedule event to occur in the future
	void Schedule( cycles_t cyclesFromNow );

	// Cancel/disable event and reset pending cycles
	void Cancel();

	// deactivates event but keeps cycles in tact
	void Pause();

	// tries to activate event with current cycles
	void Resume();

	// Check if event is currently running
	bool IsActive() const noexcept { return m_active; }

	// Get pending cycles to apply
	cycles_t GetPendingCycles() const noexcept;

	// Get remaining cycles until event triggers (will be negative if event is late)
	cycles_t GetRemainingCycles() const noexcept
	{
		return m_cyclesUntilEvent - GetPendingCycles();
	}

	// returns progress fraction from 0 to 1
	float GetProgress() const noexcept
	{
		dbAssert( m_cyclesUntilEvent > 0 );
		return std::clamp( static_cast<float>( GetPendingCycles() ) / static_cast<float>( m_cyclesUntilEvent ), 0.0f, 1.0f );
	}

	const std::string& GetName() const noexcept { return m_name; }

	void Serialize( SaveStateSerializer& serializer );

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

	void UpdateNextEvent();

	inline bool ReadyForNextEvent() const noexcept
	{
		return m_pendingCycles >= m_cyclesUntilNextEvent;
	}
	
	inline void AddCycles( cycles_t cycles ) noexcept
	{
		dbExpects( cycles > 0 );
		m_pendingCycles += cycles;
	}

	void AddCyclesAndUpdateEvents( cycles_t cycles ) noexcept
	{
		AddCycles( cycles );
		if ( ReadyForNextEvent() )
			UpdateNextEvent();
	}

	inline cycles_t GetPendingCycles() const noexcept
	{
		return m_pendingCycles;
	}

	inline void AddGteCycles( cycles_t cycles ) noexcept
	{
		m_cyclesUntilGteComplete = m_pendingCycles + cycles;
	}

	inline void StallUntilGteComplete() noexcept
	{
		m_pendingCycles = std::max( m_pendingCycles, m_cyclesUntilGteComplete );
	}

	void EndFrame();

	void Serialize( SaveStateSerializer& serializer );

private:
	void ScheduleNextEvent( const Event* event );

	void UpdateEvent( Event* event, cycles_t cycles );

	void RemoveEvent( Event* event );

	std::pair<size_t, cycles_t> FindNextEvent() const;

private:
	cycles_t m_cyclesUntilNextEvent = 0; // cached from event
	cycles_t m_pendingCycles = 0;
	cycles_t m_cyclesUntilGteComplete = 0; // need to stall CPU until GTE commands are complete. Events are too much for this
	cycles_t m_cyclesThisFrame = 0;

	std::vector<Event*> m_events;
	Event* m_nextEvent = nullptr;

	bool m_updating = false;
};

}