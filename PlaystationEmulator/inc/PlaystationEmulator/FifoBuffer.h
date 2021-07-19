#pragma once

#include <stdx/assert.h>

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

	size_t Size() const noexcept
	{
		return m_size - m_index;
	}

	bool Empty() const noexcept
	{
		return m_size == m_index;
	}

	bool Full() const noexcept
	{
		return Size() == MaxSize;
	}

	void Push( T value ) noexcept
	{
		m_buffer[ m_size++ ] = value;
	}

	void Initialize( T value ) noexcept
	{
		Reset();
		Push( value );
	}

	T Pop() noexcept
	{
		return m_buffer[ m_index++ ];
	}

	T Peek() const noexcept
	{
		return m_buffer[ m_index ];
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

	void Fill( T value ) noexcept
	{
		m_buffer.fill( value );
	}

private:
	size_t m_size = 0;
	size_t m_index = 0;
	std::array<T, MaxSize> m_buffer{};
};

}