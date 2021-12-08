#pragma once

#include <stdx/assert.h>

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace stdx
{

template <typename T>
class heap_array
{
public:
	using value_type = T;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using reference = T&;
	using const_reference = const T&;
	using pointer = T*;
	using const_pointer = const T*;

	using iterator = pointer;
	using const_iterator = const_pointer;
	using reverse_iterator = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

	// constructors / assignment

	heap_array() noexcept = default;

	heap_array( const heap_array& other )
		: m_data{ allocate_array( other.m_size ) }
		, m_size{ other.m_size }
	{}

	heap_array( heap_array&& other ) noexcept
		: m_data{ std::exchange( other.m_data, nullptr ) }
		, m_size{ std::exchange( other.m_size, 0 ) }
	{}

	heap_array( size_type n )
		: m_data{ allocate_array( n ) }
		, m_size{ n }
	{}

	heap_array( const T* p, size_type count )
		: m_data{ allocate_array( count ) }
		, m_size{ count }
	{
		std::copy_n( p, count, m_data );
	}

	template <typename InputIt, STDX_requires( !std::is_integral_v<InputIt> )
	heap_array( InputIt first, InputIt last )
		: heap_array{ static_cast<size_type>( std::distance( first, last ) ) }
	{
		std::copy( first, last, m_data );
	}

	heap_array( std::initializer_list<T> init )
		: m_data{ allocate_array( init.size() ) }
		, m_size{ init.size() }
	{
		std::copy( init.begin(), init.end(), m_data );
	}

	~heap_array()
	{
		delete[] m_data;
	}

	heap_array& operator=( const heap_array& other )
	{
		delete[] m_data;
		m_data = allocate_array( other.m_size );
		m_size = other.m_size;
		std::copy_n( other.m_data, other.m_size, m_data );
		return *this;
	}

	heap_array& operator=( heap_array&& other )
	{
		delete[] m_data;
		m_data = std::exchange( other.m_data, nullptr );
		m_size = std::exchange( other.m_size, 0 );
		return *this;
	}

	// element access

	T& at( size_type pos )
	{
		if ( pos >= m_size )
			throw std::out_of_range();

		return m_data[ pos ];
	}

	const T& at( size_type pos ) const
	{
		if ( pos >= m_size )
			throw std::out_of_range();

		return m_data[ pos ];
	}

	T& operator[]( size_type pos ) noexcept
	{
		dbExpects( pos < m_size );
		return m_data[ pos ];
	}

	const T& operator[]( size_type pos ) const noexcept
	{
		dbExpects( pos < m_size );
		return m_data[ pos ];
	}

	T& front() noexcept
	{
		dbExpects( m_size > 0 );
		return m_data[ 0 ];
	}

	const T& front() const noexcept
	{
		dbExpects( m_size > 0 );
		return m_data[ 0 ];
	}

	T& back() noexcept
	{
		dbExpects( m_size > 0 );
		return m_data[ m_size - 1 ];
	}

	const T& back() const noexcept
	{
		dbExpects( m_size > 0 );
		return m_data[ m_size - 1 ];
	}

	T* data() noexcept
	{
		return m_data;
	}

	const T* data() const noexcept
	{
		return m_data;
	}

	// iterators

	iterator begin() noexcept { return m_data; }
	iterator end() noexcept { return m_data + m_size; }

	const_iterator begin() const noexcept { return m_data; }
	const_iterator end() const noexcept { return m_data + m_size; }

	const_iterator cbegin() const noexcept { return m_data; }
	const_iterator cend() const noexcept { return m_data + m_size; }

	reverse_iterator rbegin() noexcept { return m_data + m_size; }
	reverse_iterator rend() noexcept { return m_data; }

	const_reverse_iterator rbegin() const noexcept { return m_data + m_size; }
	const_reverse_iterator rend() const noexcept { return m_data; }

	const_reverse_iterator crbegin() const noexcept { return m_data + m_size; }
	const_reverse_iterator crend() const noexcept { return m_data; }

	// capacity

	bool empty() const noexcept
	{
		return m_size == 0;
	}

	size_type size() const noexcept
	{
		return m_size;
	}

	size_type max_size() const noexcept
	{
		return static_cast<size_type>( std::numeric_limits<difference_type>::max() );
	}

	// operations

	void fill( const T& value )
	{
		std::fill_n( m_data, m_size, value );
	}

	void swap( heap_array& other )
	{
		std::swap( m_data, other.m_data );
		std::swap( m_size, other.m_size );
	}

	// non-member functions

	friend bool operator==( const heap_array& lhs, const heap_array& rhs )
	{
		return std::equal( lhs.begin(), lhs.end(), rhs.begin(), rhs.end() );
	}

	friend bool operator!=( const heap_array& lhs, const heap_array& rhs )
	{
		return !( lhs == rhs );
	}

	friend bool operator<( const heap_array& lhs, const heap_array& rhs )
	{
		return std::lexicographical_compare( lhs.begin(), lhs.end(), rhs.begin(), rhs.end() );
	}

	friend bool operator>( const heap_array& lhs, const heap_array& rhs )
	{
		return ( rhs < lhs );
	}

	friend bool operator<=( const heap_array& lhs, const heap_array& rhs )
	{
		return !( lhs > rhs );
	}

	friend bool operator>=( const heap_array& lhs, const heap_array& rhs )
	{
		return !( lhs < rhs );
	}

private:
	static T* allocate_array( size_type n )
	{
		return ( n > 0 ) ? new T[]( n ) : nullptr;
	}

private:
	T* m_data = nullptr;
	size_type m_size = 0;
};

} // namespace stdx