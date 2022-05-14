#pragma once

#include "CDXA.h"

#include <stdx/assert.h>

#include <array>
#include <cstdint>
#include <filesystem>

namespace fs = std::filesystem;

namespace PSX
{

class SaveStateSerializer;

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

	static constexpr uint8_t MinutesPerDiskBCD = 0x74;
	static constexpr uint8_t SecondsPerMinuteBCD = 0x60;
	static constexpr uint8_t SectorsPerSecondBCD = 0x75;

	static constexpr uint32_t PregapLength = 2 * SectorsPerSecond;
	static constexpr uint32_t LeadOutLength = 6750;
	static constexpr uint8_t LeadInTrackNumber = 0x00;
	static constexpr uint8_t LeadOutTrackNumber = 0xa2;

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
		std::array<uint8_t, BytesPerSector> rawData;

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

	union SubQControl
	{
		SubQControl() = default;
		SubQControl( const SubQControl& other ) : value{ other.value } {}

		struct
		{
			uint8_t adr : 4;
			uint8_t audioPreemphasis : 1;
			uint8_t digitialCopyAllowed : 1;
			uint8_t dataSector : 1;
			uint8_t fourChannelAudio : 1;
		};
		uint8_t value = 0;
	};

	struct SubQ
	{
		SubQControl control;
		uint8_t trackNumberBCD = 0;
		uint8_t trackIndexBCD = 0;
		uint8_t trackMinuteBCD = 0;
		uint8_t trackSecondBCD = 0;
		uint8_t trackSectorBCD = 0;
		uint8_t reserved = 0;
		uint8_t absoluteMinuteBCD = 0;
		uint8_t absoluteSecondBCD = 0;
		uint8_t absoluteSectorBCD = 0;
	};
	static_assert( sizeof( SubQ ) == 10 );

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

		uint32_t trackNumber = 0;
		LogicalSector position = 0;
		uint32_t length = 0; // in sectors
		uint32_t firstIndex = 0;
		Type type{};
	};

	struct Index
	{
		uint32_t indexNumber = 0;
		uint32_t trackNumber = 0;
		LogicalSector position = 0;
		LogicalSector positionInTrack = 0;
		uint32_t length = 0; // in sectors
		Track::Type trackType{};
		bool pregap = false;

		// info for multi-file formats
		uint32_t fileIndex = 0;
		uint32_t filePosition = 0;
	};

public:
	static std::unique_ptr<CDRom> Open( const fs::path& filename );
	static std::unique_ptr<CDRom> OpenBin( const fs::path& filename );
	static std::unique_ptr<CDRom> OpenCue( const fs::path& filename );

	CDRom() = default;

	CDRom( const CDRom& ) = delete;
	CDRom( CDRom&& ) = delete;
	CDRom& operator=( const CDRom& ) = delete;
	CDRom& operator=( CDRom&& ) = delete;

	virtual ~CDRom() = default;

	const fs::path& GetFilename() const noexcept { return m_filename; }

	// seek position on disk
	bool Seek( LogicalSector position ) noexcept;
	bool Seek( uint32_t trackNumber, Location locationInTrack ) noexcept;
	bool SeekTrack1() noexcept { return Seek( 1, Location{} ); }

	// read sector and increment current position
	bool ReadSector( Sector& sector, SubQ& subq );

	// read subq from current position
	bool ReadSubQ( SubQ& subq ) const;

	// read sector without updating position
	bool ReadSectorFromPosition( LogicalSector position, Sector& sector ) const;

	// read subq data from given position
	bool ReadSubQFromPosition( LogicalSector position, SubQ& subq ) const;

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

	uint32_t GetLastTrackEndPosition() const noexcept
	{
		const auto& track = m_tracks.back();
		return track.position + track.length;
	}

	LogicalSector GetTrackStartPosition( uint32_t trackNumber ) const noexcept
	{
		return m_tracks[ trackNumber - 1 ].position;
	}

	Location GetTrackStartLocation( uint32_t trackNumber ) const noexcept
	{
		return Location::FromLogicalSector( GetTrackStartPosition( trackNumber ) );
	}

	const Index* GetCurrentIndex() const noexcept
	{
		return m_currentIndex;
	}

	LogicalSector GetCurrentSeekSector() const noexcept
	{
		return m_position;
	}

	Location GetCurrentSeekLocation() const noexcept
	{
		return Location::FromLogicalSector( m_position );
	}

	LogicalSector GetCurrentTrackSector() const noexcept
	{
		return m_position - m_currentIndex->position + m_currentIndex->positionInTrack;
	}

	Location GetCurrentTrackLocation() const noexcept
	{
		return Location::FromLogicalSector( GetCurrentTrackSector() );
	}

protected:
	// best API for single or multi file formats with pregaps
	virtual bool ReadSectorFromIndex( const Index& index, LogicalSector position, Sector& sector ) const = 0;

	static SubQ GetSubQFromIndex( const Index& index, LogicalSector position ) noexcept;

	const Index* FindIndex( LogicalSector position ) const noexcept;

	void AddLeadOutIndex();

protected:
	fs::path m_filename;

	std::vector<Track> m_tracks;
	std::vector<Index> m_indices;

private:
	LogicalSector m_position = 0;

	const Index* m_currentIndex = nullptr;
	LogicalSector m_positionInTrack = 0;
	LogicalSector m_positionInIndex = 0;
};

}