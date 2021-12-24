#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace PSX
{

struct CueSheet
{
	struct TrackIndex
	{
		TrackIndex( uint8_t i, uint8_t m, uint8_t s, uint8_t f )
			: index{ i }, mm{ m }, ss{ s }, ff{ f }
		{}

		uint8_t index;
		uint8_t mm;
		uint8_t ss;
		uint8_t ff;
	};

	struct Gap
	{
		uint8_t mm = 0;
		uint8_t ss = 0;
		uint8_t ff = 0;
	};

	struct Track
	{
		enum class Type
		{
			Invalid,
			Audio,
			CDG,
			Mode1_2048,
			Mode1_2352,
			Mode2_2336,
			Mode2_2352,
			CDI_2336,
			CDI_2352
		};

		Track( uint8_t n, Type t ) : trackNumber{ n }, type{ t } {}

		uint8_t trackNumber;
		Type type;
		std::vector<TrackIndex> indices;
		Gap pregap;
		Gap postgap;
	};

	struct File
	{
		enum class Type
		{
			Invalid,
			Binary,
			Motorola,
			AIFF,
			WAVE,
			MP3
		};

		File( std::string name, Type t ) : filename{ std::move( name ) }, type{ t } {}

		std::string filename;
		Type type = Type::Invalid;
		std::vector<Track> tracks;
	};

	std::vector<File> files;
};

bool LoadCueSheet( const fs::path& filename, CueSheet& sheet );
bool ParseCueSheet( std::string_view rawtext, CueSheet& sheet );

} // namespace PSX