#pragma once

#include <stdx/assert.h>

#include <type_traits>

namespace stdx
{

// cast value to unsigned version of its type
template <typename T>
constexpr std::make_signed_t<T> unsigned_cast( T value ) noexcept
{
	static_assert( std::is_integral_v<T> );
	return static_cast<std::make_unsigned_t<T>>( value );
}

// cast value to signed version of its type
template <typename T>
constexpr std::make_signed_t<T> signed_cast( T value ) noexcept
{
	static_assert( std::is_integral_v<T> );
	return static_cast<std::make_signed_t<T>>( value );
}

// safe casting between differently signed or sized integral types
template <typename To, typename From>
constexpr To narrow_cast( From from ) noexcept
{
	static_assert( std::is_integral_v<To> );
	static_assert( std::is_integral_v<From> );

	const To result = static_cast<From>( from );

	dbAssert( ( result < 0 ) == ( from < 0 ) );
	dbAssert( static_cast<From>( result ) == from );

	return result;
}

}