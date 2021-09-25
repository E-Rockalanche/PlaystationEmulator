#pragma once

#include <string_view>

namespace stdx
{

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