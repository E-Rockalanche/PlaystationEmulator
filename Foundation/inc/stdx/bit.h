#pragma once

#include <stdx/compiler.h>
#include <stdx/cast.h>
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

template <typename T>
inline constexpr size_t bitsizeof() noexcept
{
	return sizeof( T ) * 8;
}

template <typename T>
inline constexpr size_t bitsizeof( const T& ) noexcept
{
	return bitsizeof<T>();
}

template <typename T>
inline int countl_zero( T x ) noexcept
{
#ifdef _MSC_VER
	if ( x == 0 )
		return bitsizeof<T>();

	unsigned long index;

	if constexpr ( sizeof( T ) < 8 )
		_BitScanReverse( &index, stdx::unsigned_cast( x ) );
	else
		_BitScanReverse64( &index, stdx::unsigned_cast( x ) );

	return bitsizeof<T>() - index - 1;
#endif
}

template <typename T>
constexpr int countl_one( T x ) noexcept
{
	return countl_zero<T>( ~x );
}

}