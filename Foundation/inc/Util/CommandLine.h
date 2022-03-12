#include <stdx/string.h>

#include <charconv>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Util::CommandLine
{

class CommandLineOptions;

void Initialize( int argc, char const* const* argv );
const CommandLineOptions& Get();

class CommandLineOptions
{
public:
	void Parse( int argc, char const* const* argv );

	bool HasOption( std::string_view name ) const;

	const auto& GetOptions() const { return m_entries; }

	std::optional<std::string_view> FindOption( std::string_view name ) const;

	bool FindOption( std::string_view name, std::string_view& value ) const;

	std::string_view GetOption( std::string_view name, std::string_view defaultValue ) const
	{
		std::string_view value;
		return FindOption( name, value ) ? value : defaultValue;
	}

	std::string_view GetOption( std::string_view name, const char* defaultValue ) const
	{
		return GetOption( name, std::string_view( defaultValue ) );
	}

	template <typename T>
	std::optional<T> FindOption( std::string_view name ) const;

	template <typename T>
	bool FindOption( std::string_view name, T& value ) const;

	template <typename T>
	T GetOption( std::string_view name, T defaultValue ) const
	{
		T value{};
		return FindOption( name, value ) ? value : defaultValue;
	}

private:
	bool SetupCharConv( std::string_view name, std::string_view& valueStr, int& base ) const;

private:
	struct Entry
	{
		std::string key;
		std::optional<std::string_view> value;
	};

	std::vector<Entry> m_entries;
};

template <typename T>
std::optional<T> CommandLineOptions::FindOption( std::string_view name ) const
{
	if constexpr ( std::is_arithmetic_v<T> )
	{
		std::string_view valueStr;
		int base = 10;
		if ( SetupCharConv( name, valueStr, base ) )
		{
			T value{};
			auto end = valueStr.data() + valueStr.size();
			const auto result = std::from_chars( valueStr.data(), end, value, base );
			if ( result.ptr == end && result.ec == std::errc{} )
				return value;
		}
		return std::nullopt;
	}
	else
	{
		// treat as string
		const auto str = FindOption( name );
		if ( !str.has_value() )
			return std::nullopt;

		return T( *str );
	}
}

template <typename T>
bool CommandLineOptions::FindOption( std::string_view name, T& value ) const
{
	const auto result = FindOption<T>( name );
	if ( result.has_value() )
	{
		value = *result;
		return true;
	}
	return false;
}

}