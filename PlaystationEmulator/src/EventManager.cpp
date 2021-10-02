#include "EventManager.h"

namespace PSX
{

void Event::UpdateEarly()
{
	if ( m_enabled )
	{
		m_pendingCycles += m_manager.GetPendingCycles();

		dbAssert( m_pendingCycles < m_cyclesUntilEvent ); // event should not be ready

		if ( m_pendingCycles > 0 )
		{
			Update( m_pendingCycles );

			m_manager.ScheduleNextEvent();
		}
	}
}

void Event::Schedule( cycles_t cyclesFromNow, bool resetPendingCycles )
{
	m_cyclesUntilEvent = m_manager.GetPendingCycles() + cyclesFromNow;
	m_enabled = true;

	if ( resetPendingCycles )
		m_pendingCycles = 0;

	m_manager.ScheduleNextEvent();
}

void Event::Cancel()
{
	if ( m_enabled )
	{
		m_pendingCycles = 0;
		m_cyclesUntilEvent = 0;
		m_enabled = false;

		m_manager.ScheduleNextEvent();
	}
}

cycles_t Event::GetPendingCycles() const noexcept
{
	return m_pendingCycles + m_manager.GetPendingCycles();
}

void Event::TriggerEvent()
{
	dbExpects( m_pendingCycles >= m_cyclesUntilEvent );

	// disable and trigger event
	m_enabled = false;
	Update( m_cyclesUntilEvent );

	// if event was not rescheduled, reset pending cycles
	if ( !m_enabled )
		m_pendingCycles = 0;
}

void Event::Update( cycles_t cycles )
{
	dbExpects( cycles > 0 );
	dbExpects( cycles <= m_cyclesUntilEvent );
	dbExpects( cycles <= m_pendingCycles );
	m_cyclesUntilEvent -= cycles;
	m_pendingCycles -= cycles;
	m_onUpdate( cycles );
}

EventManager::~EventManager()
{
	for ( Event* event : m_events )
		delete event;
}

Event* EventManager::CreateEvent( std::string name, EventUpdateCallback onUpdate )
{
	Event* event = new Event( *this, std::move( name ), std::move( onUpdate ) );
	m_events.push_back( event );
	return event;
}

Event* EventManager::FindEvent( std::string_view name )
{
	for ( Event* event : m_events )
		if ( event->GetName() == name )
			return event;

	return nullptr;
}

void EventManager::Reset()
{
	m_pendingCycles = 0;
	m_cyclesUntilNextEvent = 0;
	m_nextEvent = nullptr;

	for ( Event* event : m_events )
		event->Cancel();
}

void EventManager::UpdateNextEvent()
{
	// add any pending cycles to events
	if ( m_pendingCycles > 0 )
	{
		for ( Event* event : m_events )
			event->AddPendingCycles( m_pendingCycles );

		m_pendingCycles = 0;
	}

	dbAssert( m_nextEvent );
	std::exchange( m_nextEvent, nullptr )->TriggerEvent();

	// schedule next event if update did not
	if ( m_nextEvent == nullptr )
		ScheduleNextEvent();
}

void EventManager::ScheduleNextEvent()
{
	// find next event
	auto it = std::min_element( m_events.begin(), m_events.end(), []( const Event* lhs, const Event* rhs )
		{
			if ( lhs->IsEnabled() != rhs->IsEnabled() )
				return lhs->IsEnabled();

			return lhs->GetRemainingCycles() < rhs->GetRemainingCycles();
		} );
	dbAssert( it != m_events.end() );

	m_nextEvent = *it;
	dbAssert( m_nextEvent->IsEnabled() );

	m_cyclesUntilNextEvent = m_nextEvent->GetRemainingCycles(); // will be negative if next event is late
}

}