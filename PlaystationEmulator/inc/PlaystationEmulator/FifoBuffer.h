#pragma once

#include <stdx/assert.h>

#include <array>
#include <algorithm>
#include <cstdint>

namespace PSX
{

// circular queue used by PSX hardware

template <typename T, size_t BufferSize>
class FifoBuffer
{
public:
	using size_type = uint32_t;

	// element access

	T Peek() const noexcept
	{
		dbExpects( m_size > 0 );
		return m_buffer[ m_first ];
	}

	// get pointer to raw buffer
	const T* Data() const noexcept
	{
		return m_buffer.data();
	}

	// capacity

	size_type Size() const noexcept
	{
		return m_size;
	}

	bool Empty() const noexcept
	{
		return m_size == 0;
	}

	bool Full() const noexcept
	{
		return m_size == BufferSize;
	}

	size_type Capacity() const noexcept
	{
		return BufferSize - m_size;
	}

	// modifiers

	void Clear() noexcept
	{
		m_last = 0;
		m_first = 0;
		m_size = 0;
	}

	// clears queue and fills buffer with value
	void Reset( T value = T() ) noexcept
	{
		Clear();
		m_buffer.fill( value );
	}

	T Pop() noexcept
	{
		dbExpects( m_size > 0 );
		const T result = m_buffer[ m_first ];
		m_first = ( m_first + 1 ) % BufferSize;
		--m_size;
		return result;
	}

	void Push( T value ) noexcept
	{
		dbExpects( m_size < BufferSize );
		m_buffer[ m_last ] = value;
		m_last = ( m_last + 1 ) % BufferSize;
		++m_size;
	}

	void Push( const T* data, size_type count ) noexcept
	{
		dbExpects( m_size + count <= BufferSize );

		const size_type seg1Size = std::min( BufferSize - m_last, count );
		const size_type seg2Size = count - seg1Size;

		std::copy_n( data, seg1Size, m_buffer.data() + m_last );
		std::copy_n( data + seg1Size, seg2Size, m_buffer.data() );

		m_last = ( m_last + count ) % BufferSize;
		m_size += count;
	}

	void Pop( T* data, size_type count ) noexcept
	{
		dbExpects( count <= m_size );

		const size_type seg1Size = std::min( BufferSize - m_first, count );
		const size_type seg2Size = count - seg1Size;

		std::copy_n( m_buffer.data() + m_first, seg1Size, data );
		std::copy_n( m_buffer.data(), seg2Size, data + seg1Size );

		m_first = ( m_first + count ) & BufferSize;
		m_size -= count;
	}

private:
	size_type m_first = 0;
	size_type m_last = 0;
	size_type m_size = 0;
	std::array<T, BufferSize> m_buffer{};
};

}