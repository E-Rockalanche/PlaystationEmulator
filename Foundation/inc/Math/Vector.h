#pragma once

#include <stdx/assert.h>

namespace Math
{

template <typename T>
struct Vector2
{
	T x;
	T y;

	Vector2() noexcept = default;

	constexpr Vector2( const Vector2& ) noexcept = default;

	explicit constexpr Vector2( T xy ) noexcept : x{ xy }, y{ xy } {}

	constexpr Vector2( T x_, T y_ ) noexcept : x{ x_ }, y{ y_ } {}

	constexpr T& operator[]( size_t index ) noexcept
	{
		dbExpects( index < 2 );
		return ( &x )[ index ];
	}

	constexpr const T& operator[]( size_t index ) const noexcept
	{
		dbExpects( index < 2 );
		return ( &x )[ index ];
	}

	constexpr std::size_t size() const noexcept { return 2; }

	constexpr Vector2& operator*=( T value )
	{
		x *= value;
		y *= value;
		return *this;
	}

	constexpr Vector2& operator/=( T value )
	{
		x /= value;
		y /= value;
		return *this;
	}
	
	constexpr Vector2& operator*=( const Vector2& other )
	{
		x *= other.x;
		y *= other.y;
		return *this;
	}

	constexpr Vector2& operator/=( const Vector2& other )
	{
		x /= other.x;
		y /= other.y;
		return *this;
	}

	constexpr Vector2& operator+=( const Vector2& other )
	{
		x += other.x;
		y += other.y;
		return *this;
	}

	constexpr Vector2& operator-=( const Vector2& other )
	{
		x -= other.x;
		y -= other.y;
		return *this;
	}

	friend constexpr Vector2 operator*( Vector2 lhs, T rhs ) noexcept
	{
		return lhs *= rhs;
	}

	friend constexpr Vector2 operator*( T lhs, Vector2 rhs ) noexcept
	{
		return rhs *= lhs;
	}

	friend constexpr Vector2 operator/( Vector2 lhs, T rhs ) noexcept
	{
		return lhs /= rhs;
	}

	friend constexpr Vector2 operator*( Vector2 lhs, const Vector2& rhs ) noexcept
	{
		return lhs *= rhs;
	}

	friend constexpr Vector2 operator/( Vector2 lhs, const Vector2& rhs ) noexcept
	{
		return lhs /= rhs;
	}

	friend constexpr Vector2 operator+( Vector2 lhs, const Vector2& rhs ) noexcept
	{
		return lhs += rhs;
	}

	friend constexpr Vector2 operator-( Vector2 lhs, const Vector2& rhs ) noexcept
	{
		return lhs -= rhs;
	}

	friend constexpr bool operator==( const Vector2& lhs, const Vector2& rhs ) noexcept
	{
		return lhs.x == rhs.x && lhs.y == rhs.y;
	}

	friend constexpr bool operator!=( const Vector2& lhs, const Vector2& rhs ) noexcept
	{
		return !( lhs == rhs );
	}
};

using Vector2f = Vector2<float>;
using Vector2d = Vector2<double>;
using Vector2i = Vector2<int>;

template <typename T>
struct Vector3
{
	T x;
	T y;
	T z;

	Vector3() noexcept = default;
	constexpr Vector3( const Vector3& ) noexcept = default;
	explicit constexpr Vector3( T xyz ) noexcept : x{ xyz }, y{ xyz }, z{ xyz } {}
	constexpr Vector3( T x_, T y_, T z_ ) noexcept : x{ x_ }, y{ y_ }, z{ z_ } {}

	constexpr T& operator[]( size_t index ) noexcept
	{
		dbExpects( index < 3 );
		return ( &x )[ index ];
	}

	constexpr const T& operator[]( size_t index ) const noexcept
	{
		dbExpects( index < 3 );
		return ( &x )[ index ];
	}

	constexpr std::size_t size() const noexcept { return 3; }

	constexpr Vector3& operator*=( T value )
	{
		x *= value;
		y *= value;
		z *= value;
		return *this;
	}

	constexpr Vector3& operator/=( T value )
	{
		x /= value;
		y /= value;
		z /= value;
		return *this;
	}

	constexpr Vector3& operator*=( const Vector3& other )
	{
		x *= other.x;
		y *= other.y;
		z *= other.z;
		return *this;
	}

	constexpr Vector3& operator/=( const Vector3& other )
	{
		x /= other.x;
		y /= other.y;
		z /= other.z;
		return *this;
	}

	constexpr Vector3& operator+=( const Vector3& other )
	{
		x += other.x;
		y += other.y;
		z += other.z;
		return *this;
	}

	constexpr Vector3& operator-=( const Vector3& other )
	{
		x -= other.x;
		y -= other.y;
		z -= other.z;
		return *this;
	}

	friend constexpr Vector3 operator*( Vector3 lhs, T rhs ) noexcept
	{
		return lhs *= rhs;
	}

	friend constexpr Vector3 operator*( T lhs, Vector3 rhs ) noexcept
	{
		return rhs *= lhs;
	}

	friend constexpr Vector3 operator/( Vector3 lhs, T rhs ) noexcept
	{
		return lhs /= rhs;
	}

	friend constexpr Vector3 operator*( Vector3 lhs, const Vector3& rhs ) noexcept
	{
		return lhs *= rhs;
	}

	friend constexpr Vector3 operator/( Vector3 lhs, const Vector3& rhs ) noexcept
	{
		return lhs /= rhs;
	}

	friend constexpr Vector3 operator+( Vector3 lhs, const Vector3& rhs ) noexcept
	{
		return lhs += rhs;
	}

	friend constexpr Vector3 operator-( Vector3 lhs, const Vector3& rhs ) noexcept
	{
		return lhs -= rhs;
	}

	friend constexpr bool operator==( const Vector3& lhs, const Vector3& rhs ) noexcept
	{
		return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
	}

	friend constexpr bool operator!=( const Vector3& lhs, const Vector3& rhs ) noexcept
	{
		return !( lhs == rhs );
	}
};

using Vector3f = Vector3<float>;
using Vector3d = Vector3<double>;
using Vector3i = Vector3<int>;

template <typename T, typename U>
constexpr Vector2<T> VectorCast( const Vector2<U>& v ) noexcept
{
	return Vector2<T>{ static_cast<T>( v.x ), static_cast<T>( v.y ) };
}

template <typename T, typename U>
constexpr Vector3<T> VectorCast( const Vector3<U>& v ) noexcept
{
	return Vector3<T>{ static_cast<T>( v.x ), static_cast<T>( v.y ), static_cast<T>( v.z ) };
}

}