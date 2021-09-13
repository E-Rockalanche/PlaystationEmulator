#pragma once

#include "Vector.h"

#include <stdx/assert.h>

#include <array>
#include <type_traits>

namespace Math
{

template <typename T, size_t Height, size_t Width>
struct Matrix
{
	using Row = std::array<T, Width>;

	union
	{
		std::array<Row, Height> rows;
		std::array<T, Width * Height> elements;
	};

	Matrix() noexcept = default;

	constexpr Matrix( const Matrix& other ) noexcept : elements{ other.elements } {}

	explicit constexpr Matrix( T value ) noexcept
	{
		elements.fill( value );
	}

	constexpr Matrix( std::initializer_list<T> init ) noexcept
	{
		dbExpects( init.size() == Height * Width );
		std::copy_n( init.begin(), Height * Width, elements.begin() );
	}

	constexpr Row& operator[]( size_t row ) noexcept
	{
		return rows[ row ];
	}

	constexpr const Row& operator[]( size_t row ) const noexcept
	{
		return rows[ row ];
	}

	constexpr Matrix& operator+=( const Matrix& other ) noexcept
	{
		for ( size_t i = 0; i < Width * Height; ++i )
			elements[ i ] += other.elements[ i ];
		return *this;
	}

	constexpr Matrix& operator-=( const Matrix& other ) noexcept
	{
		for ( size_t i = 0; i < Width * Height; ++i )
			elements[ i ] -= other.elements[ i ];
		return *this;
	}

	constexpr Matrix& operator*=( T value ) noexcept
	{
		for ( auto& e : elements )
			e *= value;
	}

	constexpr Matrix& operator/=( T value ) noexcept
	{
		for ( auto& e : elements )
			e /= value;
	}

	friend constexpr Matrix operator+( Matrix lhs, const Matrix& rhs ) noexcept
	{
		return lhs += rhs;
	}

	friend constexpr Matrix operator-( Matrix lhs, const Matrix& rhs ) noexcept
	{
		return lhs -= rhs;
	}

	friend constexpr bool operator==( const Matrix& lhs, const Matrix& rhs ) noexcept
	{
		return lhs.elements == rhs.elements;
	}

	friend constexpr bool operator!=( const Matrix& lhs, const Matrix& rhs ) noexcept
	{
		return lhs.elements != rhs.elements;
	}
};

template <typename T, size_t H, size_t WH, size_t W>
constexpr Matrix<T, H, W> operator*( const Matrix<T, H, WH>& lhs, const Matrix<T, WH, W>& rhs ) noexcept
{
	Matrix<T, H, W> result{ 0 };

	for ( size_t j = 0; j < H; ++j )
	{
		for ( size_t i = 0; i < W; ++i )
		{
			T& cur = result[ j ][ i ];
			for ( size_t k = 0; k < WH; ++k )
				cur += lhs[ j ][ k ] * rhs[ k ][ i ];
		}
	}

	return result;
}

}