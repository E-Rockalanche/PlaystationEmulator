#pragma once

#include <stdx/assert.h>

namespace Math
{

template <typename T>
struct Rectangle
{
	T left;
	T top;
	T right;
	T bottom;

	constexpr Rectangle() noexcept = default;

	constexpr Rectangle( T l, T t, T r, T b ) noexcept : left{ l }, top{ t }, right{ r }, bottom{ b } {}

	static constexpr Rectangle FromExtents( T l, T t, T w, T h ) noexcept
	{
		return Rectangle( l, t, l + w, t + h );
	}

	constexpr bool Empty() const noexcept { return left >= right || top >= bottom; }

	constexpr T GetWidth() const noexcept { return left < right ? right - left : 0; }
	constexpr T GetHeight() const noexcept { return top < bottom ? bottom - top : 0; }

	constexpr void Grow( T x, T y ) noexcept
	{
		left = std::min( left, x );
		right = std::max( right, x );
		top = std::min( top, y );
		bottom = std::max( bottom, y );
	}

	constexpr void Grow( const Rectangle& other ) noexcept
	{
		left = std::min( left, other.left );
		right = std::max( right, other.right );
		top = std::min( top, other.top );
		bottom = std::max( bottom, other.bottom );
	}

	constexpr bool Intersects( T x, T y ) const noexcept
	{
		return ( left <= x ) && ( x < right ) && ( top <= y ) && ( y < bottom );
	}

	constexpr bool Intersects( const Rectangle& other ) const noexcept
	{
		return ( left < other.right ) && ( other.left < right ) && ( top < other.bottom ) && ( other.top < bottom );
	}

	Rectangle& operator*=( T value ) noexcept
	{
		left *= value;
		top *= value;
		right *= value;
		bottom *= value;
		return *this;
	}

	Rectangle& operator/=( T value ) noexcept
	{
		dbExpects( value != 0 );
		left /= value;
		top /= value;
		right /= value;
		bottom /= value;
		return *this;
	}

	friend Rectangle operator*( Rectangle lhs, T rhs ) noexcept
	{
		return lhs *= rhs;
	}

	friend Rectangle operator*( T lhs, Rectangle rhs ) noexcept
	{
		return rhs *= lhs;
	}

	friend Rectangle operator/( Rectangle lhs, T rhs ) noexcept
	{
		return lhs /= rhs;
	}

	friend bool operator==( const Rectangle& lhs, const Rectangle& rhs ) noexcept
	{
		return lhs.left == rhs.left && lhs.top == rhs.top && lhs.right == rhs.right && lhs.bottom == rhs.bottom;
	}

	friend bool operator!=( const Rectangle& lhs, const Rectangle& rhs ) noexcept
	{
		return !( lhs == rhs );
	}
};

}