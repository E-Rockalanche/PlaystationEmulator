#include "EventManager.h"

#include "SaveState.h"

#include <stdx/assert.h>

namespace PSX
{

namespace
{
	constexpr size_t npos = std::numeric_limits<size_t>::max();
}

Event::~Event()
{
	m_manager.RemoveEvent( this );
}

void Event::Reset()
{
	m_cyclesUntilEvent = 0;
	m_pendingCycles = 0;
	m_active = false;
}

void Event::UpdateEarly()
{
	if ( !m_active )
		return;

	cycles_t pendingCycles = m_pendingCycles + m_manager.GetPendingCycles();
	while ( pendingCycles > 0 && m_active )
	{
		const cycles_t updateCycles = std::min( pendingCycles, m_cyclesUntilEvent );
		pendingCycles -= updateCycles;
		m_manager.UpdateEvent( this, updateCycles );
	}

	m_manager.ScheduleNextEvent( this );
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
	else if ( m_cyclesUntilEvent == cyclesFromNow )
	{
		// no change, don't reschedule
		return;
	}

	m_cyclesUntilEvent = cyclesFromNow;
	m_manager.ScheduleNextEvent( this );
}

void Event::Delay( cycles_t cycles )
{
	dbExpects( cycles > 0 );
	dbAssert( m_active );

	m_cyclesUntilEvent += cycles;
	m_manager.ScheduleNextEvent( this );
}

void Event::Cancel()
{
	if ( m_active )
	{
		m_pendingCycles = 0;
		m_cyclesUntilEvent = 0;
		m_active = false;
		m_manager.ScheduleNextEvent(this );
	}
}

void Event::Pause()
{
	if ( !m_active )
		return;

	m_pendingCycles += m_manager.GetPendingCycles();
	m_active = false;
	m_manager.ScheduleNextEvent( this );
}

void Event::Resume()
{
	if ( m_active || m_cyclesUntilEvent == 0 )
		return;

	m_pendingCycles -= m_manager.GetPendingCycles();
	m_active = true;
	m_manager.ScheduleNextEvent( this );
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

void Event::Serialize( SaveStateSerializer& serializer )
{
	serializer( m_cyclesUntilEvent );
	serializer( m_pendingCycles );
	serializer( m_active );
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
	m_cyclesUntilGteComplete = 0;
	m_cyclesThisFrame = 0;
	m_nextEvent = nullptr;

	for ( Event* event : m_events )
		event->Reset();
}

void EventManager::UpdateNextEvent()
{
	dbAssert( ReadyForNextEvent() );

	if ( m_updating )
		return; // prevent recursive updates

	do
	{
		dbAssert( m_nextEvent );

		if ( m_pendingCycles > 0 )
		{
			for ( Event* event : m_events )
				event->AddPendingCycles( m_pendingCycles );

			m_cyclesUntilGteComplete = std::max( m_cyclesUntilGteComplete - m_pendingCycles, 0 );

			m_cyclesThisFrame += m_pendingCycles;
			m_pendingCycles = 0;
		}

		Event* event = std::exchange( m_nextEvent, nullptr );
		dbAssert( event->IsActive() );
		dbExpects( event->GetLocalRemainingCycles() <= 0 );

		UpdateEvent( event, event->m_cyclesUntilEvent );

		ScheduleNextEvent( nullptr );
	}
	while ( ReadyForNextEvent() );
}

void EventManager::UpdateEvent( Event* event, cycles_t cycles )
{
	dbAssert( !m_updating );
	m_updating = true;
	event->Update( cycles );
	m_updating = false;
}

void EventManager::ScheduleNextEvent( const Event* changedEvent )
{
	// prevent recursive scheduling
	// event will be scheduled after update
	if ( m_updating )
		return;

	// early out if changed event doesn't change order
	if ( m_nextEvent && changedEvent )
	{
		const auto newCyclesUntilEvent = changedEvent->IsActive() ? changedEvent->GetLocalRemainingCycles() : InfiniteCycles;
		if ( m_nextEvent == changedEvent )
		{
			if ( newCyclesUntilEvent < m_cyclesUntilNextEvent )
			{
				m_cyclesUntilNextEvent = newCyclesUntilEvent;
				return;
			}
		}
		else
		{
			if ( newCyclesUntilEvent >= m_cyclesUntilNextEvent )
			{
				return;
			}
		}
	}

	// schedule next event
	const auto [minIndex, minCycles] = FindNextEvent();
	dbAssert( minIndex != npos );
	dbAssert( minCycles != InfiniteCycles );

	m_nextEvent = m_events[ minIndex ];
	dbAssert( m_nextEvent->IsActive() );
	dbAssert( m_nextEvent->m_cyclesUntilEvent > 0 );

	m_cyclesUntilNextEvent = minCycles;
}

std::pair<size_t, cycles_t> EventManager::FindNextEvent() const
{

	// find next event
	size_t minIndex = npos;
	cycles_t minCycles = InfiniteCycles;
	for ( size_t i = 0; i < m_events.size(); ++i )
	{
		auto* event = m_events[ i ];
		dbAssert( event );
		if ( event->IsActive() )
		{
			const cycles_t remainingCycles = event->GetLocalRemainingCycles();
			if ( remainingCycles < minCycles )
			{
				minCycles = remainingCycles;
				minIndex = i;
			}
		}
	}

	return std::make_pair( minIndex, minCycles );
}

void EventManager::RemoveEvent( Event* event )
{
	dbExpects( event );
	dbLogDebug( "EventManager::RemoveEvent -- [%s]", event->GetName().c_str() );

	auto it = std::find( m_events.begin(), m_events.end(), event );
	dbAssert( it != m_events.end() );
	m_events.erase( it );
}

void EventManager::EndFrame()
{
	m_cyclesThisFrame += m_pendingCycles;
	dbLogDebug( "EventManager::EndFrame -- CPU cycles this frame: %i", m_cyclesThisFrame );
	m_cyclesThisFrame = -m_pendingCycles;
}

void EventManager::Serialize( SaveStateSerializer& serializer )
{
	dbAssert( !m_updating );

	if ( !serializer.Header( "EventManager", 1 ) )
		return;

	serializer( m_cyclesUntilNextEvent );
	serializer( m_pendingCycles );
	serializer( m_cyclesUntilGteComplete );
	serializer( m_cyclesThisFrame );

	ScheduleNextEvent( nullptr );
}

}