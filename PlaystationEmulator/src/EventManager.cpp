#include "EventManager.h"

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

	const cycles_t updateCycles = m_pendingCycles + m_manager.GetPendingCycles();

	dbAssert( updateCycles < m_cyclesUntilEvent ); // event should not be ready if it is updating early

	if ( updateCycles > 0 )
		m_manager.UpdateEvent( this, updateCycles );
}

void Event::Schedule( cycles_t cyclesFromNow )
{
	dbExpects( cyclesFromNow > 0 ); // cannot schedule event to happen immediately

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
		dbLog( "Event::Cancel -- [%s]", m_name.c_str() );

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
	dbExpects( m_manager.IsUpdating() );

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
	dbLog( "EventManager::Reset" );

	m_pendingCycles = 0;
	m_cyclesUntilNextEvent = 0;
	m_nextEvent = nullptr;

	for ( Event* event : m_events )
		event->Cancel();
}

void EventManager::UpdateNextEvent()
{
	dbAssert( m_nextEvent ); // an event should have been scheduled

	dbLog( "EventManager::UpdateNextEvent -- [%s]", m_nextEvent->GetName().c_str() );

	dbAssert( m_nextEvent->IsActive() );

	if ( m_pendingCycles > 0 )
	{
		for ( Event* event : m_events )
			event->AddPendingCycles( m_pendingCycles );

		m_pendingCycles = 0;
	}

	dbExpects( m_nextEvent->GetLocalRemainingCycles() <= 0 );
	UpdateEvent( m_nextEvent, m_nextEvent->m_cyclesUntilEvent );
}

void EventManager::UpdateEvent( Event* event, cycles_t cycles )
{
	dbExpects( cycles > 0 );
	dbExpects( event );
	dbExpects( event->IsActive() );

	m_updating = true;
	event->Update( cycles );
	m_updating = false;

	ScheduleNextEvent();
}

void EventManager::ScheduleNextEvent()
{
	if ( m_updating )
		return;

	// find next event
	auto it = std::min_element( m_events.begin(), m_events.end(), []( const Event* lhs, const Event* rhs )
		{
			if ( lhs->IsActive() != rhs->IsActive() )
				return lhs->IsActive();

			return lhs->GetLocalRemainingCycles() < rhs->GetLocalRemainingCycles();
		} );
	dbAssert( it != m_events.end() );

	m_nextEvent = *it;
	dbAssert( m_nextEvent->IsActive() );

	m_cyclesUntilNextEvent = m_nextEvent->GetLocalRemainingCycles();
}

void EventManager::RemoveEvent( Event* event )
{
	dbExpects( event );
	dbLog( "EventManager::RemoveEvent -- [%s]", event->GetName().c_str() );

	auto it = std::find( m_events.begin(), m_events.end(), event );
	dbAssert( it != m_events.end() );
	m_events.erase( it );
}

}