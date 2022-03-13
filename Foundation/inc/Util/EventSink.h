#pragma once

#include <stdx/assert.h>

#include <algorithm>
#include <functional>
#include <type_traits>
#include <vector>

namespace Util
{

namespace Detail
{
class EventSinkBase;
}

enum class EventPriority : int
{
	Lowest = std::numeric_limits<int>::min(),
	VeryLow = std::numeric_limits<int>::min() / 2,
	Low = std::numeric_limits<int>::min() / 4,
	Medium = 0,
	High = std::numeric_limits<int>::max() / 4,
	VeryHigh = std::numeric_limits<int>::max() / 2,
	Highest = std::numeric_limits<int>::max(),
};

class EventSinkSubscription
{
	friend class Detail::EventSinkBase;

public:
	EventSinkSubscription() = default;

	EventSinkSubscription( const EventSinkSubscription& ) = delete;
	EventSinkSubscription( EventSinkSubscription&& ) = delete;

	EventSinkSubscription& operator=( const EventSinkSubscription& ) = delete;
	EventSinkSubscription& operator=( EventSinkSubscription&& ) = delete;

	~EventSinkSubscription()
	{
		Clear();
	}

	// unsubscribes from all event sinks
	void Clear();

private:

	// returns true if event sink was in subscriptions
	bool Remove( Detail::EventSinkBase* eventSink )
	{
		dbAssert( eventSink );
		auto it = std::find( m_subscriptions.begin(), m_subscriptions.end(), eventSink );
		if ( it == m_subscriptions.end() )
			return false;

		m_subscriptions.erase( it );
		return true;
	}

private:

	std::vector<Detail::EventSinkBase*> m_subscriptions;
};

namespace Detail
{
	struct SubscriberPriority
	{
		EventSinkSubscription* subscriber = nullptr;
		int priority = 0;
	};

	template <typename F>
	struct EventSinkSubscriptionEntry
	{
		std::function<F> handler;
		EventSinkSubscription* subscriber = nullptr;
		int priority = 0;
		bool unsubscribed = false;

		friend bool operator<( const EventSinkSubscriptionEntry& lhs, const EventSinkSubscriptionEntry& rhs ) noexcept
		{
			return lhs.priority < rhs.priority;
		}
	};

	template <typename F>
	struct FunctionReturnType;

	template <typename R, typename... Args>
	struct FunctionReturnType<R( Args... )>
	{
		using Type = R;
	};

	class EventSinkBase
	{
		friend class Util::EventSinkSubscription;

	private:
		virtual void Remove( EventSinkSubscription* subscriber ) = 0;

		// returns true if event sink was in subscriber subscriptions
		bool RemoveFromSubscriber( EventSinkSubscription* subscriber )
		{
			dbAssert( subscriber );
			return subscriber->Remove( this );
		}
	};
}

inline void EventSinkSubscription::Clear()
{
	for ( auto* eventSink : m_subscriptions )
	{
		eventSink->Remove( this );
	}
}

// operators to create event sink subscription entry

inline Detail::SubscriberPriority operator%( EventSinkSubscription& subscriber, EventPriority priority ) noexcept
{
	return { &subscriber, static_cast<int>( priority ) };
}

template <typename F>
inline Detail::EventSinkSubscriptionEntry<F> operator%( EventSinkSubscription& subscriber, std::function<F> handler )
{
	return { std::move( handler ), &subscriber, 0 };
}

template <typename F>
inline Detail::EventSinkSubscriptionEntry<F> operator%( Detail::SubscriberPriority& subscriberPriority, std::function<F> handler )
{
	return { std::move( handler ), subscriberPriority.subscriber, subscriberPriority.priority };
}

// event sink implementation

template <typename F>
class EventSink : public Detail::EventSinkBase
{
	using ReturnType = typename Detail::FunctionReturnType<F>::Type;

	static_assert( std::is_void_v<ReturnType> || std::is_same_v<ReturnType, bool> ); // return type can only be void or bool

	static constexpr bool CanEarlyOut = std::is_same_v<ReturnType, bool>;

public:

	EventSink() = default;

	EventSink( const EventSink& ) = delete;
	EventSink( EventSink&& ) = delete;

	EventSink& operator=( const EventSink& ) = delete;
	EventSink& operator=( EventSink&& ) = delete;

	~EventSink()
	{
		for ( auto& entry : m_subscriptions )
		{
			EventSinkBase::RemoveFromSubscriber( entry.subscriber );
		}
	}

	void operator+=( Detail::EventSinkSubscriptionEntry<F>&& subscription )
	{
		dbAssert( subscription.handler );
		dbAssert( subscription.subscriber );
		dbAssert( FindSubscription( subscription.subscriber ) == m_subscriptions.end() );

		if ( m_broadcasting )
		{
			m_subscriptions.emplace_back( std::move( subscription ) );
			m_newSubscriptions++;
		}
		else
		{
			// insert in priority order
			auto it = std::lower_bound( m_subscriptions.begin(), m_subscriptions.end(), subscription );
			m_subscriptions.insert( it, std::move( subscription ) );
		}
	}

	void operator-=( EventSinkSubscription& subscriber )
	{
		// if event sink was removed from subscriber, then try to remove subscriber from event sink
		if ( EventSinkBase::RemoveFromSubscriber( &subscriber ) )
			Remove( &subscriber );
	}

	template <typename... Args>
	ReturnType operator()( Args&&... args )
	{
		m_broadcasting = true;

		// broadcast event to subscribers
		for ( size_t i = m_subscriptions.size(); i-- > 0; )
		{
			auto& subscription = m_subscriptions[ i ];

			if ( !subscription.unsubscribed )
			{
				if constexpr ( CanEarlyOut )
				{
					if ( std::invoke( subscription.handler, std::forward<Args>( args )... ) )
						return true;
				}
				else
				{
					std::invoke( subscription.handler, std::forward<Args>( args )... );
				}
			}

			if ( subscription.unsubscribed )
				m_subscriptions.erase( m_subscriptions.begin() + i );
		}

		// sort any new subscribers
		for ( size_t i = 0; i < m_newSubscriptions; ++i )
		{
			const size_t index = m_newSubscriptions.size() - i - 1;
			Detail::EventSinkSubscriptionEntry<F> subscription = std::move( m_subscriptions[ index ] );
			m_subscriptions.erase( m_subscriptions.begin() + index );

			if ( !subscription.unsubscribed )
				*this += std::move( subscription );
		}

		m_broadcasting = false;

		if constexpr ( CanEarlyOut )
			return false;
	}

private:

	void Remove( EventSinkSubscription* subscriber ) override
	{
		dbAssert( subscriber );
		auto it = FindSubscription( subscriber );
		if ( it == m_subscriptions.end() )
			return;

		if ( m_broadcasting )
			it->destroy = true;
		else
			m_subscriptions.erase( it );
	}

	auto FindSubscription( EventSinkSubscription* subscriber )
	{
		return std::find_if( m_subscriptions.begin(), m_subscriptions.end(), [subscriber]( auto& entry )
			{
				return entry.subscriber == subscriber;
			} );
	}

private:

	// events are broadcast from back to front
	// new subscriptions are added to the back of the last, and sorted in priority order later
	std::vector<Detail::EventSinkSubscriptionEntry<F>> m_subscriptions;

	// number of new subscriptions added during broadcasting
	size_t m_newSubscriptions = 0;

	bool m_broadcasting = false;
};

}