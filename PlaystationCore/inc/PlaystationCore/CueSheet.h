#pragma once

#include "CDRom.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace PSX
{

struct CueSheet
{
	static constexpr uint32_t MaxTracks = 99;
	static constexpr uint32_t MaxIndices = 99;

	struct TrackIndex
	{
		TrackIndex( uint8_t i, uint8_t m, uint8_t s, uint8_t f )
			: indexNumber{ i }, location{ m, s, f }
		{}

		uint8_t indexNumber;
		CDRom::Location location;
	};

	using Gap = CDRom::Location;

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
		std::optional<Gap> pregap;
		std::optional<Gap> postgap;

		const TrackIndex* FindIndex( uint32_t indexNumber ) const
		{
			for ( auto& index : indices )
				if ( index.indexNumber == indexNumber )
					return &index;

			return nullptr;
		}
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

	static bool Load( const fs::path& filename, CueSheet& sheet );
	static bool Parse( std::string_view rawtext, CueSheet& sheet );

	std::pair<const Track*, const File*> FindTrack( uint32_t trackNumber ) const;
};

} // namespace PSX