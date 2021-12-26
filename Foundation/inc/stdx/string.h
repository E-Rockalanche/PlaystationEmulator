#pragma once

#include <cctype>
#include <string_view>

namespace stdx
{

namespace detail
{

template <typename CharT, typename Traits>
constexpr std::basic_string_view<CharT, Traits> make_string_view( std::basic_string_view<CharT, Traits> str ) noexcept
{
	return str;
}

template <typename CharT, typename Traits = std::char_traits<CharT>>
constexpr std::basic_string_view<CharT, Traits> make_string_view( const CharT* str ) noexcept
{
	return std::basic_string_view<CharT, Traits>{ str };
}

template <typename CharT, typename Traits>
constexpr auto make_string_view( const std::basic_string<CharT, Traits>& str ) noexcept
{
	return std::basic_string_view<CharT, Traits>{ str };
}

template <typename T>
constexpr auto make_string_view( const T& str ) noexcept
{
	return std::basic_string_view( str );
}

} // namespace detail

// char traits

template <typename CharT>
struct ci_char_traits : public std::char_traits<CharT>
{
private:
	static CharT toupper( CharT c )
	{
		return static_cast<CharT>( std::toupper( static_cast<int>( static_cast<std::make_unsigned_t<CharT>>( c ) ) ) );
	}

public:
	static bool eq( CharT c1, CharT c2 ) { return toupper( c1 ) == toupper( c2 ); }
	static bool lt( CharT c1, CharT c2 ) { return toupper( c1 ) < toupper( c2 ); }

	static int compare( const CharT* s1, const CharT* s2, std::size_t count )
	{
		for ( std::size_t i = 0; i < count; ++i )
		{
			const CharT c1 = toupper( s1[ i ] );
			const CharT c2 = toupper( s2[ i ] );
			if ( c1 < c2 )
				return -1;
			else if ( c2 < c1 )
				return 1;
		}
		return 0;
	}

	static const CharT* find( const CharT* p, std::size_t count, const CharT& c )
	{
		const CharT cupper = toupper( c );
		while ( ( count > 0 ) && ( toupper( *p ) != cupper ) )
			++p;

		return p;
	}
};

// case insensitive comparison

template <typename CharT1, typename Traits1, typename CharT2, typename Traits2>
constexpr bool iequals( std::basic_string_view<CharT1, Traits1> str1, std::basic_string_view<CharT2, Traits2> str2 ) noexcept
{
	if ( str1.size() != str2.size() )
		return false;

	using CommonCharT = std::common_type_t<CharT1, CharT2>;

	const std::size_t count = str1.size();
	for ( std::size_t i = 0; i < count; ++i )
	{
		if ( !ci_char_traits<CommonCharT>::eq( str1[ i ], str2[ i ] ) )
			return false;
	}

	return true;
}

template <typename Str1, typename Str2>
constexpr bool iequals( const Str1& str1, const Str2& str2 ) noexcept
{
	return iequals( detail::make_string_view( str1 ), detail::make_string_view( str2 ) );
}

// c++20 string starts_with

template <typename CharT, typename Traits>
constexpr bool starts_with( std::basic_string_view<CharT, Traits> str, std::basic_string_view<CharT, Traits> prefix ) noexcept
{
	return str.substr( 0, prefix.size() ) == prefix;
}

template <typename CharT, typename Traits>
constexpr bool starts_with( std::basic_string_view<CharT, Traits> str, CharT c ) noexcept
{
	return !str.empty() && Traits::eq( str.front(), c );
}

template <typename CharT, typename Traits>
constexpr bool starts_with( std::basic_string_view<CharT, Traits> str, const CharT* prefix ) noexcept
{
	return starts_with( str, std::basic_string_view<CharT, Traits>( prefix ) );
}

// c++20 string ends_with

template <typename CharT, typename Traits>
constexpr bool ends_with( std::basic_string_view<CharT, Traits> str, std::basic_string_view<CharT, Traits> suffix ) noexcept
{
	return str.size() >= suffix.size() && ( str.compare( str.size() - suffix.size(), str.npos, suffix ) == 0 );
}

template <typename CharT, typename Traits>
constexpr bool ends_with( std::basic_string_view<CharT, Traits> str, CharT c ) noexcept
{
	return !str.empty() && Traits::eq( str.back(), c );
}

template <typename CharT, typename Traits>
constexpr bool ends_with( std::basic_string_view<CharT, Traits> str, const CharT* suffix ) noexcept
{
	return ends_with( str, std::basic_string_view<CharT, Traits>( suffix ) );
}

// c++20 string contains

template <typename CharT, typename Traits>
constexpr bool contains( std::basic_string_view<CharT, Traits> str, std::basic_string_view<CharT, Traits> substr ) noexcept
{
	return str.find( substr ) != str.npos;
}

template <typename CharT, typename Traits>
constexpr bool contains( std::basic_string_view<CharT, Traits> str, CharT c ) noexcept
{
	return str.find( c ) != str.npos;
}

template <typename CharT, typename Traits>
constexpr bool contains( std::basic_string_view<CharT, Traits> str, const CharT* substr ) noexcept
{
	return str.find( substr ) != str.npos;
}

}