#pragma once

namespace Math
{

template <typename T>
struct Vector2
{
	T x;
	T y;

	Vector2() noexcept = default;

	constexpr Vector2( const Vector2& ) noexcept = default;

	constexpr Vector2( T xy ) noexcept : x{ xy }, y{ xy } {}

	constexpr Vector2( T x_, T y_ ) noexcept : x{ x_ }, y{ y_ } {}
	
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

}