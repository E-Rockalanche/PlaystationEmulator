#pragma once

#include <stdx/assert.h>

#include <cstring>
#include <type_traits>

namespace stdx
{

// reinterpret value of type From as Type To using non-UB casting
template <typename To, typename From, STDX_requires(
	( sizeof( To ) == sizeof( From ) ) &&
	std::is_trivially_copyable_v<To> &&
	std::is_trivially_copyable_v<From> )
// requires compiler magic to be constexpr
inline To bit_cast( From value ) noexcept
{
	static_assert( std::is_trivially_constructible_v<To> ); // This implementation also requires type To to be trivially constructable

	To result;
	std::memcpy( &result, &value, sizeof( To ) );
	return result;
}

// cast value to unsigned version of its type
template <typename T, STDX_requires( std::is_integral_v<T> )
constexpr std::make_signed_t<T> unsigned_cast( T value ) noexcept
{
	return static_cast<std::make_unsigned_t<T>>( value );
}

// cast value to signed version of its type
template <typename T, STDX_requires( std::is_integral_v<T> )
constexpr std::make_signed_t<T> signed_cast( T value ) noexcept
{
	return static_cast<std::make_signed_t<T>>( value );
}

// safe casting between differently signed or sized integral types
template <typename To, typename From, STDX_requires(
	std::is_integral_v<To> &&
	std::is_integral_v<From> )
constexpr To narrow_cast( From from ) noexcept
{
	static_assert( ( sizeof( To ) < sizeof( From ) ) || ( std::is_signed_v<To> != std::is_signed_v<From> ) ); // narrow_cast is unnecessary otherwise

	const To result = static_cast<From>( from );

	dbAssert( ( result < 0 ) == ( from < 0 ) ); // ensure sign is preserved
	dbAssert( static_cast<From>( result ) == from ); // ensure value is preserved

	return result;
}

} // namespace stdx