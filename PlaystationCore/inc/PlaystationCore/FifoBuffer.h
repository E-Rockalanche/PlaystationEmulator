#pragma once

#include <stdx/assert.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <memory>

namespace PSX
{

// circular queue used by PSX hardware

namespace Detail
{

template <typename T, size_t BufferSize, bool UseHeap>
struct FifoBufferStorage;

template <typename T, size_t BufferSize>
struct FifoBufferStorage<T, BufferSize, false>
{
	using size_type = uint32_t;

	T* GetData() noexcept { return m_data.data(); }
	const T* GetData() const noexcept { return m_data.data(); }

	T& operator[]( size_type index ) noexcept { return m_data[ index ]; }
	const T& operator[]( size_type index ) const noexcept { return m_data[ index ]; }

	std::array<T, BufferSize> m_data;
};

template <typename T, size_t BufferSize>
struct FifoBufferStorage<T, BufferSize, true>
{
	using size_type = uint32_t;

	FifoBufferStorage() : m_data{ std::make_unique<T[]>( BufferSize ) } {}

	FifoBufferStorage( const FifoBufferStorage& other ) : FifoBufferStorage()
	{
		std::copy_n( other.m_data.get(), BufferSize, m_data.get() );
	}

	FifoBufferStorage( FifoBufferStorage&& ) = delete;

	FifoBufferStorage& operator=( const FifoBufferStorage& other )
	{
		std::copy_n( other.m_data.get(), BufferSize, m_data.get() );
	}

	FifoBufferStorage& operator=( FifoBufferStorage&& ) = delete;

	T* GetData() noexcept { return m_data.get(); }
	const T* GetData() const noexcept { return m_data.get(); }

	T& operator[]( size_type index ) noexcept { dbExpects( index < BufferSize ); return m_data[ index ]; }
	const T& operator[]( size_type index ) const noexcept { dbExpects( index < BufferSize ); return m_data[ index ]; }

	std::unique_ptr<T[]> m_data;
};

} // namespace Detail

template <typename T, size_t BufferSize>
class FifoBuffer
{
	static constexpr bool UseHeap = ( BufferSize > 32 );
	using StorageType = Detail::FifoBufferStorage<T, BufferSize, UseHeap>;

public:
	using value_type = T;
	using size_type = typename StorageType::size_type;

	// construction/assignment

	FifoBuffer() = default;

	FifoBuffer( const FifoBuffer& ) = default;

	FifoBuffer( FifoBuffer&& ) = delete;

	FifoBuffer( std::initializer_list<T> init ) noexcept
	{
		dbExpects( init.size() <= BufferSize );
		std::copy( init.begin(), init.end(), m_storage.GetData() );
		m_size = init.size();
		m_last = init.size() % BufferSize;
	}

	FifoBuffer& operator=( const FifoBuffer& ) noexcept = default;

	FifoBuffer& operator=( FifoBuffer&& ) = delete;

	FifoBuffer& operator=( std::initializer_list<T> init ) noexcept
	{
		dbExpects( init.size() <= BufferSize );
		std::copy( init.begin(), init.end(), m_storage.GetData() );
		m_size = init.size();
		m_last = init.size() % BufferSize;
		m_first = 0;
		return *this;
	}

	// element access

	T Peek() const noexcept
	{
		dbExpects( m_size > 0 );
		return m_storage[ m_first ];
	}

	T operator[]( size_type i ) const noexcept
	{
		dbExpects( i < m_size );
		const size_type pos = ( m_first + i ) % BufferSize;
		return m_storage[ pos ];
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
		std::fill_n( m_storage.GetData(), BufferSize, value );
	}

	T Pop() noexcept
	{
		dbExpects( m_size > 0 );
		const T result = m_storage[ m_first ];
		m_first = ( m_first + 1 ) % BufferSize;
		--m_size;
		return result;
	}

	void Push( T value ) noexcept
	{
		dbExpects( m_size < BufferSize );
		m_storage[ m_last ] = value;
		m_last = ( m_last + 1 ) % BufferSize;
		++m_size;
	}

	void Push( const T* data, size_type count ) noexcept
	{
		dbExpects( m_size + count <= BufferSize );

		const size_type seg1Size = std::min( BufferSize - m_last, count );
		const size_type seg2Size = count - seg1Size;

		std::copy_n( data, seg1Size, m_storage.GetData() + m_last );
		std::copy_n( data + seg1Size, seg2Size, m_storage.GetData() );

		m_last = ( m_last + count ) % BufferSize;
		m_size += count;
	}

	void Pop( T* data, size_type count ) noexcept
	{
		dbExpects( count <= m_size );

		const size_type seg1Size = std::min( BufferSize - m_first, count );
		const size_type seg2Size = count - seg1Size;

		std::copy_n( m_storage.GetData() + m_first, seg1Size, data );
		std::copy_n( m_storage.GetData(), seg2Size, data + seg1Size );

		m_first = ( m_first + count ) % BufferSize;
		m_size -= count;
	}

	void Ignore( size_type count ) noexcept
	{
		dbExpects( count <= m_size );
		m_first = ( m_first + count ) % BufferSize;
		m_size -= count;
	}

private:
	size_type m_first = 0;
	size_type m_last = 0;
	size_type m_size = 0;
	StorageType m_storage{};
};

}