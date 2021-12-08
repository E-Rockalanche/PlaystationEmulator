#pragma once

#include <string>
#include <vector>

namespace PSX
{

class CueSheet
{
public:
	struct TrackIndex
	{
		uint8_t index = 0xff;
		uint8_t mm = 0xff;
		uint8_t ss = 0xff;
		uint8_t ff = 0xff;
	};

	struct Track
	{
		enum class Type
		{
			Invalid,
			Mode2_2352,
			Audio
		};

		uint8_t trackNumber = 0xff;
		Type type = Type::Invalid;
		std::vector<TrackIndex> indices;
	};

	struct File
	{
		enum class Type
		{
			Invalid,
			Binary
		};

		std::string filename;
		Type type = Type::Invalid;
		std::vector<Track> tracks;
	};

	const TrackIndex& GetTrackIndex( uint8_t track, uint8_t index )
	{

	}

private:

	std::vector<File> m_files;
	std::vector<size_t> m_trackFileMapping;
};

}