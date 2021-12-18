#include "EventManager.h"

#include <stdx/assert.h>

namespace PSX
{

Event::~Event()
{
	m_manager.RemoveEvent( this );
}

void Event::UpdateEarly()
{
	if ( !m_active )
		return;

	cycles_t pendingCycles = m_pendingCycles + m_manager.GetPendingCycles();
	while( pendingCycles > 0 )
	{
		const cycles_t updateCycles = std::min( pendingCycles, m_cyclesUntilEvent );
		pendingCycles -= updateCycles;
		m_manager.UpdateEvent( this, updateCycles );
		m_manager.ScheduleNextEvent();
	}
}

void Event::Schedule( cycles_t cyclesFromNow )
{
	dbExpects( cyclesFromNow > 0 ); // cannot schedule event to happen immediately

	dbLogDebug( "Event::Schedule -- [%s] [%i]", m_name.c_str(), cyclesFromNow );

	if ( !m_active )
	{
		// timer just started, so we must delay by the manager's current pending cycles
		m_pendingCycles = -m_manager.GetPendingCycles();
		m_active = true;
	}

	m_cyclesUntilEvent = cyclesFromNow;
	m_manager.ScheduleNextEvent();
}

void Event::Cancel()
{
	if ( m_active )
	{
		dbLogDebug( "Event::Cancel -- [%s]", m_name.c_str() );

		m_pendingCycles = 0;
		m_cyclesUntilEvent = 0;
		m_active = false;
		m_manager.ScheduleNextEvent();
	}
}

cycles_t Event::GetPendingCycles() const noexcept
{
	dbExpects( m_active );
	return m_pendingCycles + m_manager.GetPendingCycles();
}

void Event::Update( cycles_t cycles )
{
	dbExpects( m_active );
	dbExpects( cycles > 0 );
	dbExpects( cycles <= m_cyclesUntilEvent );

	dbLogDebug( "Event::Update -- [%s] [%i]", m_name.c_str(), cycles );

	m_cyclesUntilEvent -= cycles;
	m_pendingCycles -= cycles;

	m_onUpdate( cycles );

	// if event was not re-scheduled, disable it
	if ( m_cyclesUntilEvent == 0 )
	{
		m_pendingCycles = 0;
		m_active = false;
	}
}

EventManager::~EventManager()
{
	dbAssert( m_events.empty() );
}

EventHandle EventManager::CreateEvent( std::string name, EventUpdateCallback onUpdate )
{
	dbExpects( !name.empty() );
	EventHandle event( new Event( *this, std::move( name ), std::move( onUpdate ) ) );
	m_events.push_back( event.get() );
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
	dbLogDebug( "EventManager::Reset" );

	m_pendingCycles = 0;
	m_cyclesUntilNextEvent = 0;
	m_nextEvent = nullptr;

	for ( Event* event : m_events )
		event->Cancel();
}

void EventManager::UpdateNextEvent()
{
	dbAssert( m_nextEvent ); // an event should have been scheduled

	if ( m_pendingCycles > 0 )
	{
		for ( Event* event : m_events )
			event->AddPendingCycles( m_pendingCycles );

		m_pendingCycles = 0;
	}

	Event* event = std::exchange( m_nextEvent, nullptr );
	dbAssert( event->IsActive() );
	dbExpects( event->GetLocalRemainingCycles() <= 0 );

	UpdateEvent( event, event->m_cyclesUntilEvent );

	ScheduleNextEvent();
}

void EventManager::UpdateEvent( Event* event, cycles_t cycles )
{
	dbAssert( !m_updating );
	m_updating = true;

	event->Update( cycles );
	m_updating = false;
}

void EventManager::ScheduleNextEvent()
{
	if ( m_updating )
		return; // event will be scheduled after update

	// find next event
	auto it = std::min_element( m_events.begin(), m_events.end(), []( const Event* lhs, const Event* rhs )
		{
			if ( lhs->IsActive() != rhs->IsActive() )
				return lhs->IsActive();

			return lhs->GetLocalRemainingCycles() < rhs->GetLocalRemainingCycles();
		} );

	if ( it == m_events.end() )
		return;

	Event* event = *it;
	if ( !event->IsActive() )
		return;

	dbAssert( event->m_cyclesUntilEvent > 0 );

	m_nextEvent = event;

	// cache cycles until event
	m_cyclesUntilNextEvent = event->GetLocalRemainingCycles();
}

void EventManager::RemoveEvent( Event* event )
{
	dbExpects( event );
	dbLogDebug( "EventManager::RemoveEvent -- [%s]", event->GetName().c_str() );

	auto it = std::find( m_events.begin(), m_events.end(), event );
	dbAssert( it != m_events.end() );
	m_events.erase( it );
}

}