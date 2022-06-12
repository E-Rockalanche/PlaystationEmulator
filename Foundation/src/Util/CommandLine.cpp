#include <Util/CommandLine.h>

#include <stdx/assert.h>

#include <algorithm>
#include <cctype>

namespace Util::CommandLine
{

namespace
{

inline bool IsIdentifierChar( char c )
{
	return std::isalnum( c ) != 0 || ( c == '_' );
}

const char* const InvalidArgFormatStr = "Invalid command line argument [%s]";
const char* const AddedArgFormatStr = "Added command line option [%s]";

CommandLineOptions g_commandLine;

}

void Initialize( int argc, char const* const* argv )
{
	g_commandLine.Parse( argc, argv );
}

const CommandLineOptions& Get()
{
	return g_commandLine;
}

void CommandLineOptions::Parse( int argc, char const* const* argv )
{
	for ( int i = 1; i < argc; ++i )
	{
		std::string_view arg = argv[ i ];

		// scan arg name
		size_t pos = 0;
		while ( pos < arg.size() && IsIdentifierChar( arg[ pos ] ) )
			++pos;

		if ( pos == 0 )
		{
			LogWarning( InvalidArgFormatStr, arg.data() );
			continue;
		}

		auto& entry = m_entries.emplace_back();
		entry.key = std::string( arg, 0, pos );

		// check for equal sign

		if ( pos == arg.size() )
		{
			Log( AddedArgFormatStr, arg.data() );
			continue;
		}

		if ( arg[ pos ] != '=' )
		{
			LogWarning( InvalidArgFormatStr, arg.data() );
			continue;
		}

		++pos;
		std::string_view value = arg.substr( pos );
		if ( value.size() >= 2 && value.front() == '"' && value.back() == '"' )
			value = value.substr( 1, value.size() - 2 );

		entry.value = value;
		Log( AddedArgFormatStr, arg.data() );
	}
}

bool CommandLineOptions::HasOption( std::string_view name ) const
{
	return std::any_of( m_entries.begin(), m_entries.end(), [name]( auto& entry ) { return entry.key == name; } );
}

std::optional<std::string_view> CommandLineOptions::FindOption( std::string_view name ) const
{
	for ( const auto& entry : m_entries )
		if ( entry.key == name )
			return entry.value;

	return std::nullopt;
}

bool CommandLineOptions::FindOption( std::string_view name, std::string_view& value ) const
{
	const auto result = FindOption( name );
	if ( result.has_value() )
	{
		value = *result;
		return true;
	}
	return false;
}

bool CommandLineOptions::SetupCharConv( std::string_view name, std::string_view& valueStr, int& base ) const
{
	static constexpr int Base16 = 16;
	static constexpr int Base8 = 8;
	static constexpr int Base2 = 2;

	if ( !FindOption( name, valueStr ) || valueStr.empty() )
		return false;

	if ( valueStr.front() == '0' )
	{
		if ( valueStr[ 1 ] == 'x' || valueStr[ 1 ] == 'X' )
		{
			valueStr.remove_prefix( 2 );
			base = Base16;
		}
		else if ( valueStr[ 1 ] == 'b' || valueStr[ 1 ] == 'B' )
		{
			valueStr.remove_prefix( 2 );
			base = Base2;
		}
		else
		{
			base = Base8;
		}
	}

	return true;
}

}