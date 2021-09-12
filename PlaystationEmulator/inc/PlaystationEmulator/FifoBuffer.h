#pragma once

#include <stdx/assert.h>

#include <array>
#include <algorithm>
#include <cstdint>

namespace PSX
{

template <typename T, size_t BufferSize>
class FifoBuffer
{
public:
	using size_type = uint32_t;

	// element access

	T Pop() noexcept
	{
		dbExpects( !Empty() );
		return m_buffer[ m_first++ ];
	}

	T Peek() const noexcept
	{
		dbExpects( !Empty() );
		return m_buffer[ m_first ];
	}

	const T* Data() const noexcept
	{
		return m_buffer.data();
	}

	// capacity

	size_type Size() const noexcept
	{
		return m_last - m_first;
	}

	bool Empty() const noexcept
	{
		return m_last == m_first;
	}

	bool Full() const noexcept
	{
		return Size() == BufferSize;
	}

	size_type Capacity() const noexcept
	{
		return BufferSize - m_last;
	}

	size_type MaxSize() noexcept
	{
		return BufferSize;
	}

	// modifiers

	void Clear() noexcept
	{
		m_last = 0;
		m_first = 0;
	}

	void Reset( T value = T() ) noexcept
	{
		Clear();
		m_buffer.fill( value );
	}

	void Push( T value ) noexcept
	{
		m_buffer[ m_last++ ] = value;
	}

	void Push( const T* data, size_type count ) noexcept
	{
		dbExpects( count <= Capacity() );
		std::copy_n( data, count, m_buffer.data() + m_last );
		m_last += count;
	}

	// move current position back to regain access to the last popped element
	void Unpop() noexcept
	{
		dbExpects( m_first > 0 );
		--m_first;
	}
	
	// shift remaining data to front of buffer
	void Shift() noexcept
	{
		if ( m_first > 0 )
			std::copy_n( m_buffer.data() + m_first, Size(), m_buffer.data() );
	}

private:
	size_type m_first = 0;
	size_type m_last = 0;
	std::array<T, BufferSize> m_buffer{};
};

}