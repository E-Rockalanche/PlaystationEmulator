#pragma once

#include <stdx/type_traits.h>

#include <memory>

namespace stdx
{

// c++20 make_unique_for_overwrite

template <typename T, STDX_requires( !std::is_array_v<T> )
std::unique_ptr<T> make_unique_for_overwrite()
{
	return std::unique_ptr<T>( new T );
}

template <typename T, STDX_requires( stdx::is_unbounded_array_v<T> )
std::unique_ptr<T> make_unique_for_overwrite( std::size_t size )
{
	return std::unique_ptr<T>( new std::remove_extent_t<T>[ size ] );
}

template <typename T, typename... Args, STDX_requires( stdx::is_bounded_array_v<T> )
std::unique_ptr<T> make_unique_for_overwrite( Args&&... ) = delete;

// c++20 to_address

template <typename T>
constexpr auto to_address( const T& p ) noexcept
{
	return to_address( p.operator->() );
}

template <typename T>
constexpr T* to_address( T* p ) noexcept
{
	static_assert( !std::is_function_v<T> );
	return p;
}

}