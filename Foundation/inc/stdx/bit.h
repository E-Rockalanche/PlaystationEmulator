#pragma once

#include <stdx/compiler.h>
#include <stdx/cast.h>
#include <type_traits>

#include <algorithm>
#include <intrin.h>

namespace stdx
{

// c++20 endian
enum class endian
{
#ifdef _WIN32
	little = 0,
	big = 1,
	native = little
#else
	little = __ORDER_LITTLE_ENDIAN__,
	big = __ORDER_BIG_ENDIAN__,
	native = __BYTE_ORDER__
#endif
};

// c++23 byteswap
template <typename T, STDX_requires( std::is_integral_v<T> )
constexpr T byteswap( T n ) noexcept
{
	if constexpr ( sizeof( T ) == 1 )
		return n;

	T result;
	const char* src = reinterpret_cast<const char*>( &n );
	char* dest = reinterpret_cast<char*>( &result );

	for ( size_t i = 0; i < sizeof( T ); ++i )
		dest[ i ] = src[ sizeof( T ) - i - 1 ];

	return result;
}

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
inline constexpr size_t bitsizeof( const T& obj ) noexcept
{
	(void)obj;
	return bitsizeof<T>();
}

template <typename T, STDX_requires( std::is_integral_v<T> )
inline int countl_zero( T x ) noexcept
{
#ifdef _MSC_VER
	if ( x == 0 )
		return (int)bitsizeof<T>();

	if constexpr ( sizeof( T ) == 1 )
		return (int)__lzcnt16( static_cast<int16_t>( static_cast<uint8_t>( x ) ) ) - 8;

	if constexpr ( sizeof( T ) == 2 )
		return (int)__lzcnt16( static_cast<int16_t>( x ) );

	if constexpr ( sizeof( T ) == 4 )
		return (int)__lzcnt( static_cast<int32_t>( x ) );

	if constexpr ( sizeof( T ) == 8 )
	{
		const int msb = (int)__lzcnt( static_cast<int32_t>( ( x >> 32 ) & 0xffffffff ) );
		if ( msb < 32 )
			return msb;
		else
			return 32 + (int)__lzcnt( static_cast<int32_t>( x & 0xffffffff ) );
	}

#else
	static_assert( false ); // unimplemented
#endif
}

template <typename T>
constexpr int countl_one( T x ) noexcept
{
	return countl_zero<T>( ~x );
}

}