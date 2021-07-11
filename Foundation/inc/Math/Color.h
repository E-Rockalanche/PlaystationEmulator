#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace Math
{

template <typename T>
struct ColorTraits
{
	static constexpr T Zero = T( 0 );
	static constexpr T Max = std::is_floating_point_v<T> ? T( 1 ) : std::numeric_limits<T>::max();
};

template <typename To, typename From>
constexpr To ConvertColorComponent( From from ) noexcept
{
	return static_cast<To>( ( from * ColorTraits<To>::Max ) / ColorTraits<From>::Max );
}

template <typename T>
struct ColorRGB
{
	using Traits = ColorTraits<T>;

	T r;
	T g;
	T b;

	ColorRGB() noexcept = default;

	explicit constexpr ColorRGB( T rgb ) noexcept : r{ rgb }, g{ rgb }, b{ rgb } {}

	constexpr ColorRGB( T r_, T g_, T b_ ) noexcept : r{ r_ }, g{ g_ }, b{ b_ } {}

	T& operator[]( size_t index ) noexcept
	{
		return ( &r )[ index ];
	}

	const T& operator[]( size_t index ) const noexcept
	{
		return ( &r )[ index ];
	}

	friend constexpr bool operator==( const ColorRGB& lhs, const ColorRGB& rhs ) noexcept
	{
		return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
	}

	friend constexpr bool operator!=( const ColorRGB& lhs, const ColorRGB& rhs ) noexcept
	{
		return !( lhs == rhs );
	}
};

template <typename T>
constexpr ColorRGB<T> FromColorCode( uint32_t rgb ) noexcept
{
	return ColorRGB
	{
		ConvertColorComponent<T, uint8_t>( rgb & 0xff ),
		ConvertColorComponent<T, uint8_t>( ( rgb >> 8 ) & 0xff ),
		ConvertColorComponent<T, uint8_t>( ( rgb >> 16 ) & 0xff )
	};
}

template <typename T>
constexpr uint32_t ToColorCode( const ColorRGB<T>& color ) noexcept
{
	return ConvertColorComponent<uint8_t, T>( color.r ) |
		( ConvertColorComponent<uint8_t, T>( color.g ) << 8 ) |
		( ConvertColorComponent<uint8_t, T>( color.b ) << 16 );
}

template <typename To, typename From>
constexpr ColorRGB<To> ColorCast( const ColorRGB<From>& from ) noexcept
{
	return ColorRGB
	{
		ConvertColorComponent<To, From>( from.r ),
		ConvertColorComponent<To, From>( from.g ),
		ConvertColorComponent<To, From>( from.b )
	};
}

}