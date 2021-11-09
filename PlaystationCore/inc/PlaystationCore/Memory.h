#pragma once

#include <stdx/assert.h>

#include <array>
#include <cstdint>
#include <type_traits>

namespace PSX
{

template <size_t MemorySize>
class Memory
{
	static_assert( MemorySize % 4 == 0 );

public:
	uint8_t& operator[]( uint32_t offset ) noexcept
	{
		return m_data[ offset ];
	}

	const uint8_t& operator[]( uint32_t offset ) const noexcept
	{
		return m_data[ offset ];
	}

	uint8_t* Data() noexcept { return m_data.data(); }

	const uint8_t* Data() const noexcept { return m_data.data(); }

	template <typename T>
	inline T Read( uint32_t offset ) const noexcept
	{
		dbExpects( offset % sizeof( T ) == 0 );
		dbExpects( offset < MemorySize );
		return *reinterpret_cast<const T*>( m_data.data() + offset );
	}

	template <typename T>
	inline void Write( uint32_t offset, T value ) noexcept
	{
		dbExpects( offset % sizeof( T ) == 0 );
		dbExpects( offset < MemorySize );
		*reinterpret_cast<T*>( m_data.data() + offset ) = value;
	}

	void Fill( uint8_t value ) noexcept
	{
		m_data.fill( value );
	}

	static constexpr uint32_t Size() noexcept
	{
		return static_cast<uint32_t>( MemorySize );
	}

private:
	std::array<uint8_t, MemorySize> m_data{};
};

}