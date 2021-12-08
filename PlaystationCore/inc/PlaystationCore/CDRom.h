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

constexpr bool IsValidBCDAndLess( uint8_t bcd, uint8_t maximum )
{
	dbExpects( IsValidBCD( maximum ) );
	return IsValidBCDDigit( bcd & 0x0f ) && bcd < maximum;
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

	static constexpr uint32_t BytesPerSector = 0x930;
	static constexpr uint32_t RawDataBytesPerSector = 0x924; // includes headers
	static constexpr uint32_t DataBytesPerSector = 0x800; // excludes headers

	static constexpr uint32_t SyncSize = 0x0c;
	static constexpr uint32_t HeaderSize = 4;
	static constexpr uint32_t SubHeaderSize = 4;

	using Sync = std::array<uint8_t, SyncSize>;

	struct Header
	{
		uint8_t minute;		// BCD
		uint8_t second;		// BCD
		uint8_t sector;		// BCD
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
					std::array<uint8_t, 0x114> errorCorrectionCodes;
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
							std::array<uint8_t, 0x114> errorCorrectionCodes;
						} form1;

						struct
						{
							std::array<uint8_t, 0x914> data;
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
		static Location FromBCD( uint8_t mm, uint8_t ss, uint8_t sect ) noexcept
		{
			return Location{ BCDToBinary( mm ), BCDToBinary( ss ), BCDToBinary( sect ) };
		}

		uint32_t GetLogicalSector() const noexcept
		{
			return minute * SectorsPerMinute + second * SectorsPerSecond + sector;
		}

		uint8_t minute = 0;
		uint8_t second = 0;
		uint8_t sector = 0;
	};

public:
	bool Open( const fs::path& filename )
	{
		m_file.open( filename, std::ios::binary );
		return m_file.is_open();
	}

	bool IsOpen() const
	{
		return m_file.is_open();
	}

	void Close()
	{
		m_file.close();
	}

	void Seek( uint32_t logicalSector )
	{
		uint32_t physicalSector = ( logicalSector <= SectorsPerSecond * 2 ) ? 0 : logicalSector - SectorsPerSecond * 2;
		m_file.seekg( static_cast<std::streampos>( physicalSector ) * BytesPerSector );
	}

	bool ReadSector( Sector& sector )
	{
		if ( m_file.eof() )
			return false;

		m_file.read( (char*)&sector, sizeof( Sector ) );
		return true;
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

private:
	std::ifstream m_file;
};

}