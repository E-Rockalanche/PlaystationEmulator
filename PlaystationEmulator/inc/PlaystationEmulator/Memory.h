#pragma once

#include "assert.h"

#include <cstdint>
#include <type_traits>

namespace PSX
{

template <size_t MemorySize>
class Memory
{
	static_assert( MemorySize % 4 == 0 );
public:
	using value_type = char;
	using size_type = uint32_t;
	using pointer = char*;
	using const_pointer = const char*;
	using reference = char&;
	using const_reference = const char&;

	char& operator[]( size_type offset ) noexcept
	{
		dbExpects( offset < MemorySize );
		return m_data[ offset ];
	}

	const char& operator[]( size_type offset ) const noexcept
	{
		dbExpects( offset < MemorySize );
		return m_data[ offset ];
	}

	char* Data() noexcept { return m_data; }

	const char* Data() const noexcept { return m_data; }

	template <typename T>
	T Read( size_type offset ) const noexcept
	{
		dbExpects( offset <= MemorySize - sizeof( T ) );
		return *reinterpret_cast<const T*>( m_data + offset );
	}

	template <typename T>
	void Write( size_type offset, T value ) noexcept
	{
		dbExpects( offset <= MemorySize - sizeof( T ) );
		*reinterpret_cast<T*>( m_data + offset ) = value;
	}

	void Fill( char value ) noexcept
	{
		for ( auto& byte : m_data )
			byte = value;
	}

	void Fill( uint32_t value ) noexcept
	{
		uint32_t* data = reinterpret_cast<uint32_t*>( m_data );
		for ( const auto last = data + MemorySize / 4; data != last; ++data )
			*data = value;
	}

	static constexpr size_type Size() noexcept { return static_cast<uint32_t>( MemorySize ); }

private:
	char m_data[ MemorySize ];
};

}