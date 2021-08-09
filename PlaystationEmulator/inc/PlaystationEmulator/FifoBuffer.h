#pragma once

#include <stdx/assert.h>

#include <array>
#include <cstdint>

namespace PSX
{

template <typename T, size_t BufferSize>
class FifoBuffer
{
public:
	using size_type = uint32_t;

	void Reset() noexcept
	{
		m_size = 0;
		m_index = 0;
	}

	size_type Size() const noexcept
	{
		return m_size - m_index;
	}

	bool Empty() const noexcept
	{
		return m_size == m_index;
	}

	bool Full() const noexcept
	{
		return Size() == BufferSize;
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
		dbExpects( !Empty() );
		return m_buffer[ m_index++ ];
	}

	T Peek() const noexcept
	{
		dbExpects( !Empty() );
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

	size_type Capacity() const noexcept
	{
		return BufferSize - m_size;
	}

	static constexpr size_type MaxSize() noexcept { return BufferSize; }

private:
	size_type m_size = 0;
	size_type m_index = 0;
	std::array<T, BufferSize> m_buffer{};
};

}