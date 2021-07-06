#pragma once

#include <stdx/compiler.h>

#include <type_traits>

namespace Math
{

struct FromRaw_t
{
	explicit FromRaw_t() = default;
};
inline constexpr FromRaw_t FromRaw;

template <typename T, size_t FractionBits>
class FixedPoint
{
	using UnsignedType = std::make_unsigned_t<T>;
	static constexpr bool IsSigned = std::is_signed_v<T>;
	static constexpr UnsignedType FractionMask = ( UnsignedType( 1 ) << FractionBits ) - 1;
	static constexpr UnsignedType WholeMask = ~FractionMask;

	static constexpr T One = T( 1 ) << FractionBits;

	static_assert( FractionBits <= sizeof( T ) * 8 - IsSigned );

	template <typename U, size_t F>
	friend class FixedPoint<U, F>;

public:
	FixedPoint() noexcept = default;

	constexpr FixedPoint( const FixedPoint& other ) = default;

	template <typename U>
	explicit constexpr FixedPoint( U value ) noexcept : m_value{ static_cast<T>( value * One ) } {}

	template <typename U, size_t F>
	explicit constexpr FixedPoint( FixedPoint<U, F> other ) noexcept : m_value{ static_cast<T>( ( other.m_value * One ) / other::One ) } {}

	constexpr FixedPoint( FromRaw_t, T value ) : m_value{ value } {}

	explicit constexpr operator T() const noexcept
	{
		return m_value / One;
	}

	T GetRaw() const noexcept { return m_value; }

	constexpr FixedPoint& operator+=( T value ) noexcept
	{
		m_value += static_cast<T>( value * One );
		return *this;
	}

	constexpr FixedPoint& operator-=( T value ) noexcept
	{
		m_value -= static_cast<T>( value * One );
		return *this;
	}

	constexpr FixedPoint& operator*=( T value ) noexcept
	{
		m_value *= value;
		return *this;
	}

	constexpr FixedPoint& operator/=( T value ) noexcept
	{
		m_value /= value;
		return *this;
	}

	constexpr FixedPoint& operator+=( FixedPoint other ) noexcept
	{
		m_value += other.m_value;
		return *this;
	}

	constexpr FixedPoint& operator-=( FixedPoint other ) noexcept
	{
		m_value -= other.m_value;
		return *this;
	}

	constexpr FixedPoint& operator*=( FixedPoint other ) noexcept
	{
		m_value = static_cast<T>( ( m_value * other.m_value ) / One );
		return *this;
	}

	constexpr FixedPoint& operator/=( FixedPoint other ) noexcept
	{
		m_value = static_cast<T>( ( m_value * One ) / other.m_value );
		return *this;
	}

	template <typename U, size_t F>
	constexpr FixedPoint& operator+=( FixedPoint<U, F> other ) noexcept
	{
		m_value += static_cast<T>( ( other.m_value * One ) / other::One );
		return *this;
	}

	template <typename U, size_t F>
	constexpr FixedPoint& operator-=( FixedPoint<U, F> other ) noexcept
	{
		m_value -= static_cast<T>( ( other.m_value * One ) / other::One );
		return *this;
	}

	template <typename U, size_t F>
	constexpr FixedPoint& operator*=( FixedPoint<U, F> other ) noexcept
	{
		m_value = static_cast<T>( ( m_value * other.m_value ) / other::One );
		return *this;
	}

	template <typename U, size_t F>
	constexpr FixedPoint& operator/=( FixedPoint<U, F> other ) noexcept
	{
		m_value = static_cast<T>( ( m_value * other::One ) / other.m_value );
		return *this;
	}

	// comparison

	friend constexpr bool operator==( FixedPoint lhs, FixedPoint rhs ) noexcept
	{
		return lhs.m_value == rhs.m_value;
	}

	friend constexpr bool operator!=( FixedPoint lhs, FixedPoint rhs ) noexcept
	{
		return lhs.m_value != rhs.m_value;
	}

	friend constexpr bool operator<( FixedPoint lhs, FixedPoint rhs ) noexcept
	{
		return lhs.m_value < rhs.m_value;
	}

	friend constexpr bool operator>( FixedPoint lhs, FixedPoint rhs ) noexcept
	{
		return lhs.m_value > rhs.m_value;
	}

	friend constexpr bool operator<=( FixedPoint lhs, FixedPoint rhs ) noexcept
	{
		return lhs.m_value <= rhs.m_value;
	}

	friend constexpr bool operator>=( FixedPoint lhs, FixedPoint rhs ) noexcept
	{
		return lhs.m_value >= rhs.m_value;
	}

private:
	T m_value;
};

}