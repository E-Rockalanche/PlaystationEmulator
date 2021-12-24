#pragma once

#include "CDXA.h"

#include <stdx/assert.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace PSX
{

constexpr bool IsValidBCDDigit( uint8_t digit )
{
	return digit <= 0x09;
}

constexpr bool IsValidBCD( uint8_t bcd )
{
	return IsValidBCDDigit( bcd & 0x0f ) && IsValidBCDDigit( bcd >> 4 );
}

constexpr bool IsValidBCDAndLess( uint8_t bcd, uint8_t maximumBCD )
{
	dbExpects( IsValidBCD( maximumBCD ) );
	return IsValidBCDDigit( bcd & 0x0f ) && bcd < maximumBCD;
}

constexpr uint8_t BCDToBinary( uint8_t bcd )
{
	dbExpects( IsValidBCD( bcd ) );
	return ( bcd & 0xf ) + ( bcd >> 4 ) * 10;
}

constexpr uint8_t BinaryToBCD( uint8_t binary )
{
	dbExpects( binary <= 99 );
	return ( binary % 10 ) | ( ( binary / 10 ) << 4 );
}

class CDRom
{

public:
	static constexpr uint32_t MinutesPerDisk = 74;
	static constexpr uint32_t SecondsPerMinute = 60;
	static constexpr uint32_t SectorsPerSecond = 75;
	static constexpr uint32_t SectorsPerMinute = SecondsPerMinute * SectorsPerSecond;

	static constexpr uint32_t MinutesPerDiskBCD = 0x74;
	static constexpr uint32_t SecondsPerMinuteBCD = 0x60;
	static constexpr uint32_t SectorsPerSecondBCD = 0x75;

	static constexpr uint32_t BytesPerSector = 0x930; // (2352)
	static constexpr uint32_t RawDataBytesPerSector = 0x924; // (2340) includes headers
	static constexpr uint32_t DataBytesPerSector = 0x800; // (2048) excludes headers
	static constexpr uint32_t Mode2Form1DataBytesPerSector = 0x914; // (2324)
	static constexpr uint32_t ErrorCorrectionCodesSize = 0x114;

	static constexpr uint32_t SyncSize = 0x0c;
	static constexpr uint32_t HeaderSize = 4;
	static constexpr uint32_t SubHeaderSize = 4;

	using LogicalSector = uint32_t;

	using Sync = std::array<uint8_t, SyncSize>;

	struct Header
	{
		uint8_t minuteBCD;
		uint8_t secondBCD;
		uint8_t sectorBCD;
		uint8_t mode;
	};

	union Sector
	{
		std::array<uint8_t, BytesPerSector> audio;

		struct
		{
			Sync sync;
			Header header;

			union
			{
				struct
				{
					std::array<uint8_t, DataBytesPerSector> data;
					uint32_t checksum;
					std::array<uint8_t, 8> zeroFilled;
					std::array<uint8_t, ErrorCorrectionCodesSize> errorCorrectionCodes;
				} mode1;

				struct
				{
					CDXA::SubHeader subHeader;
					CDXA::SubHeader subHeaderCopy;

					union
					{
						struct
						{
							std::array<uint8_t, DataBytesPerSector> data;
							uint32_t checksum;
							std::array<uint8_t, ErrorCorrectionCodesSize> errorCorrectionCodes;
						} form1;

						struct
						{
							std::array<uint8_t, Mode2Form1DataBytesPerSector> data;
							uint32_t checksum;
						} form2;
					};

				} mode2;
			};
		};
	};
	static_assert( sizeof( Sector ) == BytesPerSector );

	struct Location
	{
		static constexpr Location FromBCD( uint8_t mm, uint8_t ss, uint8_t sect ) noexcept
		{
			return Location{ BCDToBinary( mm ), BCDToBinary( ss ), BCDToBinary( sect ) };
		}

		static constexpr Location FromLogicalSector( LogicalSector pos ) noexcept
		{
			const uint8_t sector = pos % SectorsPerSecond;
			pos /= SectorsPerSecond;

			const uint8_t second = pos % SecondsPerMinute;
			pos /= SecondsPerMinute;

			const uint8_t minute = static_cast<uint8_t>( pos );

			return Location{ minute, second, sector };
		}

		LogicalSector ToLogicalSector() const noexcept
		{
			return static_cast<LogicalSector>( minute * SectorsPerMinute + second * SectorsPerSecond + sector );
		}

		uint8_t minute = 0;
		uint8_t second = 0;
		uint8_t sector = 0;
	};

	struct Track
	{
		enum class Type
		{
			Audio,

			Mode1_2048,
			Mode1_2352,

			Mode2_2336,
			Mode2_2048,
			Mode2_2324,
			Mode2_2332,
			Mode2_2352,
		};

		uint32_t trackNumber;
		LogicalSector position;
		uint32_t length; // in sectors
		uint32_t firstIndex;
		Type type;
	};

	struct Index
	{
		uint32_t indexNumber;
		uint32_t trackNumber;
		LogicalSector position;
		LogicalSector positionInTrack;
		uint32_t length; // in sectors
		Track::Type trackType;
		bool pregap;
	};

public:
	bool Open( const fs::path& filename )
	{
		m_file.open( filename, std::ios::binary );
		if ( !m_file.is_open() )
			return false;

		// create TOC
		m_file.seekg( 0, std::ios::end );
		const auto fileSize = static_cast<uint32_t>( m_file.tellg() );
		m_file.seekg( 0, std::ios::beg );
		const uint32_t totalSectors = fileSize / BytesPerSector;

		m_tracks.push_back( Track{ 1, 0, totalSectors, 1, Track::Type::Mode2_2352 } );
		m_indices.push_back( Index{ 1, 1, 0, 0, totalSectors,Track::Type::Mode2_2352, false } );

		return true;
	}

	bool IsOpen() const
	{
		return m_file.is_open();
	}

	void Close()
	{
		m_file.close();
	}

	void Seek( LogicalSector position )
	{
		m_position = position;
		const uint32_t physicalSector = ( position <= SectorsPerSecond * 2 ) ? 0 : position - SectorsPerSecond * 2;
		m_file.seekg( static_cast<std::streampos>( physicalSector ) * BytesPerSector );
	}

	bool ReadSector( Sector& sector )
	{
		if ( m_file.eof() )
			return false;

		m_file.read( (char*)&sector, sizeof( Sector ) );
		return true;
	}

	uint32_t GetTrackCount() const noexcept
	{
		return static_cast<uint32_t>( m_tracks.size() );
	}

	uint32_t GetFirstTrackNumber() const noexcept
	{
		return m_tracks.front().trackNumber;
	}

	uint32_t GetLastTrackNumber() const noexcept
	{
		return m_tracks.back().trackNumber;
	}

	uint32_t GetLogicalSectorCount() const noexcept
	{
		auto& lastTrack = m_tracks.back();
		return lastTrack.position + lastTrack.length;
	}

	uint32_t GetTrackStartPosition( uint32_t trackNumber ) const noexcept
	{
		return m_tracks[ trackNumber - 1 ].position;
	}

private:
	template <typename T>
	T Read()
	{
		dbExpects( !m_file.eof() );
		T obj;
		m_file.read( reinterpret_cast<char*>( &obj ), sizeof( obj ) );
		return obj;
	}

protected:
	fs::path m_filename;

	std::vector<Track> m_tracks;
	std::vector<Index> m_indices;

private:
	std::ifstream m_file;

	LogicalSector m_position = 0;

	const Index* m_currentIndex = nullptr;
	LogicalSector m_positionInTrack = 0;
	LogicalSector m_positionInIndex = 0;
};

}