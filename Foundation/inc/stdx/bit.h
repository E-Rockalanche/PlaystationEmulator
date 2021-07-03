#pragma once

#include "compiler.h"
#include <type_traits>

namespace stdx
{

template <typename T, STDX_requires( std::is_integral_v<T> )
inline constexpr bool any_of( T value, T flags ) noexcept
{
	return ( value & flags ) != 0;
}

template <typename T, STDX_requires( std::is_integral_v<T> )
inline constexpr bool all_of( T value, T flags ) noexcept
{
	return ( value & flags ) == flags;
}

template <typename T, STDX_requires( std::is_integral_v<T> )
inline constexpr bool none_of( T value, T flags ) noexcept
{
	return ( value & flags ) == 0;
}

template <typename T, STDX_requires( std::is_integral_v<T> )
inline constexpr void set_bit( T& value, size_t bit, bool set = true ) noexcept
{
	if ( set )
		value |= 1 << bit;
	else
		value &= ~( 1 << bit );
}

template <typename T, STDX_requires( std::is_integral_v<T> )
	inline constexpr void reset_bit( T& value, size_t bit ) noexcept
{
	value &= ~( 1 << bit );
}

template <typename T, STDX_requires( std::is_integral_v<T> )
	inline constexpr void set_bits( T& value, T flags, bool set = true ) noexcept
{
	if ( set )
		value |= flags;
	else
		value &= ~flags;
}

template <typename T, STDX_requires( std::is_integral_v<T> )
inline constexpr void reset_bits( T& value, T flags ) noexcept
{
	value &= ~flags;
}

template <typename T, STDX_requires( std::is_integral_v<T> )
inline constexpr void masked_set( T& value, T mask, T flags ) noexcept
{
	value = ( value & ~mask ) | ( flags & mask );
}

}