#pragma once

#include <stdx/compiler.h>

#include <array>
#include <cstdint>

namespace PSX::CDXA
{

constexpr uint32_t AdpcmChunks = 18;
constexpr uint32_t AdpcmChunkSize = 128;
constexpr uint32_t AdpcmWordsPerChunk = 28;
constexpr uint32_t AdpcmSamplesPerSector4Bit = AdpcmChunks * AdpcmWordsPerChunk * 8;
constexpr uint32_t AdpcmSamplesPerSector8Bit = AdpcmChunks * AdpcmWordsPerChunk * 4;

union SubMode
{
	struct
	{
		uint8_t endOfRecord : 1;	// all volume descriptors, and all sectors with EOF

		// Sector type
		uint8_t video : 1;
		uint8_t audio : 1;
		uint8_t data : 1;

		uint8_t trigger : 1;		// for application use
		uint8_t form2 : 1;			// 0: 0x800 data bytes, 1: 0x914 data bytes
		uint8_t realTime : 1;
		uint8_t endOfFile : 1;		// or end of directory, path table, volume terminator
	};
	uint8_t value;
};
static_assert( sizeof( SubMode ) == 1 );

union CodingInfo
{
	uint32_t GetSampleRate() const { return sampleRate != 0u ? 18900 : 37800; }

	struct
	{
		uint8_t stereo : 2;		// 0=Mono, 1=Stereo, 2-3=Reserved
		uint8_t sampleRate : 2;		// 0=37800Hz, 1=18900Hz, 2-3=Reserved
		uint8_t bitsPerSample : 2;	// 0=Normal/4bit, 1=8bit, 2-3=Reserved
		uint8_t emphasis : 1;		// 0=Normal/Off, 1=Emphasis
		uint8_t : 1;
	};
	uint8_t value;
};
static_assert( sizeof( CodingInfo ) == 1 );

union SubHeader
{
	struct
	{
		uint8_t file;			// (0x00-0xff) (for audio/video interleave)
		uint8_t channel;		// (0x00-0x1f) (for audio/video interleave)
		SubMode subMode;
		CodingInfo codingInfo;
	};
	std::array<uint8_t, 4> data;
};
static_assert( sizeof( SubHeader ) == 4 );

void DecodeAdpcmSector( const SubHeader& subHeader, const uint8_t* data, int32_t* inOutOldSamples, int16_t* outSamples );

}