#pragma once

#include <stdx/assert.h>

#include <chrono>

namespace Util
{

class Stopwatch
{
public:
	using Clock = std::chrono::high_resolution_clock;
	using TimePoint = Clock::time_point;
	using Duration = Clock::duration;

	bool IsStopped() const
	{
		return m_stopped;
	}

	Duration GetElapsed() const
	{
		return m_duration + ( Clock::now() - m_start );
	}

	void Start( Duration duration = {} )
	{
		m_start = Clock::now();
		m_duration = duration;
		m_stopped = false;
	}

	void Stop()
	{
		if ( !m_stopped )
		{
			m_stopped = true;
			m_duration += Clock::now() - m_start;
		}
	}

	void Resume()
	{
		if ( m_stopped )
		{
			m_start = Clock::now();
			m_stopped = false;
		}
	}

	void Reset()
	{
		m_start = {};
		m_duration = {};
		m_stopped = true;
	}

private:
	TimePoint m_start{};
	Duration m_duration{};
	bool m_stopped = true;
};

}