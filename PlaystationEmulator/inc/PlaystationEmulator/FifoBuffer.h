#pragma once

#include "assert.h"

#include <array>
#include <cstdint>

namespace PSX
{

template <typename T, size_t MaxSize>
class FifoBuffer
{
public:
	void Reset() noexcept
	{
		m_size = 0;
		m_index = 0;
	}

	bool Empty() const noexcept
	{
		return m_size == 0;
	}

	bool Full() const noexcept
	{
		return m_size == MaxSize;
	}

	void Push( T value ) noexcept
	{
		dbExpects( m_size < MaxSize );
		m_buffer[ m_size++ ] = value;
	}

	void Initialize( T value ) noexcept
	{
		Reset();
		Push( value );
	}

	T Pop() noexcept
	{
		dbExpects( m_index < m_size );
		return m_buffer[ m_index++ ];
	}

	void Unpop() noexcept
	{
		dbExpects( m_index > 0 );
		--m_index;
	}

	const T* Data() const noexcept
	{
		return m_buffer.data();
	}

private:
	size_t m_size = 0;
	size_t m_index = 0;
	std::array<T, MaxSize> m_buffer;
};

}