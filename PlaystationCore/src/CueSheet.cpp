#include "CueSheet.h"

#include <stdx/assert.h>
#include <stdx/memory.h>

#include <cctype>
#include <fstream>

using namespace std::string_view_literals;

namespace PSX
{

bool LoadCueSheet( const fs::path& filename, CueSheet& sheet )
{
	std::ifstream fin( filename );
	if ( !fin.is_open() )
	{
		dbLogWarning( "LoadCueSheet -- failed to load cue sheet from %s", filename.u8string().c_str() );
		return false;
	}

	// get file size
	fin.seekg( 0, std::ios::end );
	const size_t size = static_cast<size_t>( fin.tellg() );
	fin.seekg( 0, std::ios::beg );

	// read into buffer
	auto rawtext = stdx::make_unique_for_overwrite<char[]>( size );
	fin.read( rawtext.get(), size );

	fin.close();

	return ParseCueSheet( std::string_view( rawtext.get(), size ), sheet );
}

bool ParseCueSheet( std::string_view rawtext, CueSheet& sheet )
{
	size_t pos = 0;

	static constexpr uint8_t InvalidBCD = 0xff;

	auto skipWhitespace = [&]
	{
		while ( pos < rawtext.size() && std::isspace( rawtext[ pos ] ) )
			++pos;
	};

	auto readToken = [&]
	{
		skipWhitespace();

		const size_t start = pos;
		while ( pos < rawtext.size() && !std::isspace( rawtext[ pos ] ) )
			++pos;

		return rawtext.substr( start, pos - start );
	};

	auto readDelimeter = [&]( char delimeter )
	{
		// don't skip whitespace

		if ( pos < rawtext.size() && rawtext[ pos ] == delimeter )
		{
			++pos;
			return true;
		}
		return false;
	};

	auto readString = [&]
	{
		skipWhitespace();

		if ( !readDelimeter( '"' ) )
			return std::string_view{};

		++pos;
		const size_t start = pos;
		while ( pos < rawtext.size() && ( rawtext[ pos ] != '"' ) );
			++pos;

		if ( !readDelimeter( '"' ) )
			return std::string_view{};

		const size_t size = pos - start;
		++pos;

		return rawtext.substr( start, size );
	};

	auto readFileType = [&]
	{
		using Type = CueSheet::File::Type;

		skipWhitespace();

		const auto token = readToken();
		if ( token == "BINARY"sv )
			return Type::Binary;
		else if ( token == "MOTOROLA"sv )
			return Type::Motorola;
		else if ( token == "AIFF"sv )
			return Type::AIFF;
		else if ( token == "WAVE"sv )
			return Type::WAVE;
		else if ( token == "MP3"sv )
			return Type::MP3;
		else
			return Type::Invalid;
	};

	auto readBCD = [&]
	{
		skipWhitespace();

		if ( ( rawtext.size() - pos ) < 2 )
			return InvalidBCD;

		const uint8_t upper = static_cast<uint8_t>( rawtext[ pos++ ] - '0' );
		const uint8_t lower = static_cast<uint8_t>( rawtext[ pos++ ] - '0' );

		if ( lower > 9 || upper > 9 )
			return InvalidBCD;

		return static_cast<uint8_t>( lower + upper * 10 );
	};

	auto readTrackType = [&]
	{
		using Type = CueSheet::Track::Type;

		skipWhitespace();

		const auto token = readToken();
		if ( token == "AUDIO"sv )
			return Type::Audio;
		else if ( token == "CDG"sv )
			return Type::CDG;
		else if ( token == "MODE1/2048"sv )
			return Type::Mode1_2048;
		else if ( token == "MODE1/2352"sv )
			return Type::Mode1_2352;
		else if ( token == "MODE2/2336"sv )
			return Type::Mode2_2336;
		else if ( token == "MODE2/2352"sv )
			return Type::Mode2_2352;
		else if ( token == "CDI/2336"sv )
			return Type::CDI_2336;
		else if ( token == "CDI/2352"sv )
			return Type::CDI_2352;
		else
			return Type::Invalid;
	};

	auto readMMSSFF = [&]
	{
		// ff will be invalid if anything fails
		const uint8_t mm = readBCD();
		readDelimeter( ':' );
		const uint8_t ss = readBCD();
		readDelimeter( ':' );
		const uint8_t ff = readBCD();
		return std::make_tuple( mm, ss, ff );
	};

	std::vector<CueSheet::File> files;

	CueSheet::File* currentFile = nullptr;
	CueSheet::Track* currentTrack = nullptr;

	for(;;)
	{
		const auto token = readToken();
		if ( token.empty() )
			break;

		if ( token == "FILE"sv )
		{
			const auto filename = readString();
			if ( filename.empty() )
				return false;

			const auto type = readFileType();
			if ( type == CueSheet::File::Type::Invalid )
				return false;

			currentFile = &files.emplace_back( std::string( filename ), type );
			currentTrack = nullptr;
		}
		else if ( token == "TRACK"sv )
		{
			if ( !currentFile )
				return false;

			const uint8_t trackNumber = readBCD();
			if ( trackNumber == InvalidBCD )
				return false;

			const auto type = readTrackType();
			if ( type == CueSheet::Track::Type::Invalid )
				return false;

			currentTrack = &currentFile->tracks.emplace_back( trackNumber, type );
		}
		else if ( token == "INDEX"sv )
		{
			if ( !currentTrack )
				return false;

			const uint8_t index = readBCD();
			if ( index == InvalidBCD )
				return false;

			const auto [mm, ss, ff] = readMMSSFF();
			if ( ff == InvalidBCD )
				return false;

			currentTrack->indices.emplace_back( index, mm, ss, ff );
		}
		else if ( token == "PREGAP"sv )
		{
			if ( !currentTrack )
				return false;

			const auto [mm, ss, ff] = readMMSSFF();
			if ( ff == InvalidBCD )
				return false;

			currentTrack->pregap = { mm, ss, ff };
		}
		else if ( token == "POSTGAP"sv )
		{
			if ( !currentTrack )
				return false;

			const auto [mm, ss, ff] = readMMSSFF();
			if ( ff == InvalidBCD )
				return false;

			currentTrack->postgap = { mm, ss, ff };
		}
		else if ( token == "REM"sv )
		{
			// ignore comment
			while ( pos < rawtext.size() && rawtext[ pos ] != '\n' )
				++pos;
		}
	}

	sheet.files = std::move( files );
	return true;
}

} // namespace PSX