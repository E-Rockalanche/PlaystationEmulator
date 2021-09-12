#pragma once

#include <stdx/assert.h>

#include <array>
#include <cstdint>
#include <fstream>

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
	static constexpr uint32_t RawBytesPerSector = 0x930;
	static constexpr uint32_t DataBytesPerSector = 0x800; // excludes sector encoding (CDROM, CD-XA Form1, CD-XA Form2)

	static constexpr uint32_t MinutesPerDiskBCD = 0x74;
	static constexpr uint32_t SecondsPerMinuteBCD = 0x60;
	static constexpr uint32_t SectorsPerSecondBCD = 0x75;

	using Sync = std::array<char, 0x0c>;

	struct Location
	{
		static Location FromBCD( uint8_t mm, uint8_t ss, uint8_t sect )
		{
			return Location{ BCDToBinary( mm ), BCDToBinary( ss ), BCDToBinary( sect ) };
		}

		uint8_t minute = 0;
		uint8_t second = 0;
		uint8_t sector = 0;
	};

	struct Header
	{
		uint8_t minute = 0; // BCD
		uint8_t second = 0; // BCD
		uint8_t sector = 0; // BCD
		uint8_t mode = 0;
	};

	enum SubMode : uint8_t
	{
		EndOfRecord = 1 << 0, // all volume descriptors, and all sectors with EOF

		// Sector type
		Video = 1 << 1,
		Audio = 1 << 2,
		Data = 1 << 3,

		Trigger = 1 << 4, // for application use
		Form2 = 1 << 5, // 0: 0x800 data bytes, 1: 0x914 data bytes
		RealTime = 1 << 6,
		EndOfFile = 1 << 7, // or end of directory, path table, volume terminator
	};

	struct SubHeader
	{
		uint8_t file = 0; // (0x00-0xff) (for audio/video interleave)
		uint8_t channel = 0; // (0x00-0x1f) (for audio/video interleave)
		uint8_t submode = 0;
		uint8_t codingInfo = 0;
	};

	bool Open( const char* filename )
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

	void Seek( const Location& location )
	{
		// CDROM starts at 2 seconds
		const uint32_t sector = location.minute * SectorsPerMinute + location.second * SectorsPerSecond + location.sector - 2 * SectorsPerSecond;
		const uint32_t address = sector * RawBytesPerSector;

		m_file.seekg( address );
	}

	void Read( char* data, std::size_t count )
	{
		m_file.read( data, count );
	}

	bool VerifySync()
	{
		static const Sync SyncBytes{ 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0 };

		Sync sync = Read<Sync>();
		return sync == SyncBytes;
	}

	Header ReadHeader()
	{
		return Read<Header>();
	}

	SubHeader ReadSubHeader()
	{
		return Read<SubHeader>();
	}

private:
	template <typename T>
	T Read()
	{
		T obj;
		m_file.read( reinterpret_cast<char*>( &obj ), sizeof( obj ) );
		return obj;
	}

private:
	std::ifstream m_file;
};

}