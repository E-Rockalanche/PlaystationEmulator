#pragma once

#include <stdx/assert.h>

#include <cstdint>
#include <type_traits>

namespace PSX
{

template <size_t MemorySize>
class Memory
{
	static_assert( MemorySize % 4 == 0 );

public:
	char& operator[]( uint32_t offset ) noexcept
	{
		dbExpects( offset < MemorySize );
		return m_data[ offset ];
	}

	const char& operator[]( uint32_t offset ) const noexcept
	{
		dbExpects( offset < MemorySize );
		return m_data[ offset ];
	}

	char* Data() noexcept { return m_data; }

	const char* Data() const noexcept { return m_data; }

	template <typename T>
	T Read( uint32_t offset ) const noexcept
	{
		dbExpects( offset % sizeof( T ) == 0 );
		dbExpects( offset < MemorySize );
		return *reinterpret_cast<const T*>( m_data + offset );
	}

	template <typename T>
	void Write( uint32_t offset, T value ) noexcept
	{
		dbExpects( offset % sizeof( T ) == 0 );
		dbExpects( offset < MemorySize );
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

	static constexpr uint32_t Size() noexcept { return static_cast<uint32_t>( MemorySize ); }

private:
	char m_data[ MemorySize ];
};

}