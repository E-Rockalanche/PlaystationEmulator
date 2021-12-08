#include "CDXA.h"

#include <array>
#include <algorithm>

namespace PSX::CDXA
{

namespace
{

constexpr uint32_t AdpcmChunkHeaderSize = 16;

union BlockHeader
{
	BlockHeader( uint8_t v ) noexcept : value{ v } {}

	uint8_t GetShift() const noexcept
	{
		uint8_t s = shift;
		return ( s < 13 ) ? s : 9;
	}

	struct
	{
		uint8_t shift : 4;		// (0..12) (0=Loudest) (13..15=Reserved/Same as 9)
		uint8_t filter : 2;		// (0..3) (only four filters, unlike SPU-ADPCM which has five)
		uint8_t : 2;
	};
	uint8_t value;
};
static_assert( sizeof( BlockHeader ) == 1 );

// XA_ADPCM only supports 4 filters
constexpr std::array<int32_t, 4> AdpcmPosTable{ 0, 60, 115, 98 };
constexpr std::array<int32_t, 4> AdpcmNegTable{ 0, 0, -52, -55 };

template <bool Is8Bit, bool IsStereo>
void DecodeAdpcmChunk( const uint8_t* chunk, int32_t* inOutOldSamples, int16_t* outSamples )
{
	static constexpr uint32_t NumBlocks = Is8Bit ? 4 : 8;

	/*
	00h..03h  Copy of below 4 bytes (at 04h..07h)
	04h       Header for 1st Block/Mono, or 1st Block/Left
	05h       Header for 2nd Block/Mono, or 1st Block/Right
	06h       Header for 3rd Block/Mono, or 2nd Block/Left
	07h       Header for 4th Block/Mono, or 2nd Block/Right
	08h       Header for 5th Block/Mono, or 3rd Block/Left  ;\unknown/unused
	09h       Header for 6th Block/Mono, or 3rd Block/Right ; for 8bit ADPCM
	0Ah       Header for 7th Block/Mono, or 4th Block/Left  ; (maybe 0, or maybe
	0Bh       Header for 8th Block/Mono, or 4th Block/Right ;/copy of above)
	0Ch..0Fh  Copy of above 4 bytes (at 08h..0Bh)
	*/
	const uint8_t* headers = chunk + 4;
	const uint8_t* data = chunk + AdpcmChunkHeaderSize;

	for ( uint32_t block = 0; block < NumBlocks; ++block )
	{
		const BlockHeader blockHeader{ headers[ block ] };

		const uint8_t shift = blockHeader.GetShift();
		const uint8_t filter = blockHeader.filter;

		const int32_t posFilter = AdpcmPosTable[ filter ];
		const int32_t negFilter = AdpcmNegTable[ filter ];

		int16_t* curOutSamples = outSamples + ( IsStereo ? ( block / 2 ) * ( AdpcmWordsPerChunk * 2 ) + ( block % 2 ) : block * AdpcmWordsPerChunk );

		constexpr size_t sampleIncrement = IsStereo ? 2 : 1;

		for ( size_t i = 0; i < AdpcmWordsPerChunk; ++i )
		{
			const uint32_t word = *reinterpret_cast<const uint32_t*>( data + i * 4 );

			const uint32_t nibble = Is8Bit
				? ( ( word >> ( block * 8 ) ) & 0xff )
				: ( ( word >> ( block * 4 ) ) & 0x0f );

			const int16_t sample = static_cast<int16_t>( ( nibble << 12 ) & 0xffff ) >> shift;

			// mix in old values
			int32_t* curOldSamples = inOutOldSamples + ( IsStereo ? ( block % 2 ) * 2 : 0 );
			const int32_t mixedSample = static_cast<int32_t>( sample ) + ( curOldSamples[ 0 ] * posFilter + curOldSamples[ 1 ] * negFilter + 32 ) / 64;
			curOldSamples[ 1 ] = std::exchange( curOldSamples[ 0 ], mixedSample );

			*curOutSamples = static_cast<int16_t>( std::clamp<int32_t>( mixedSample, std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max() ) );
			curOutSamples += sampleIncrement;
		}
	}
}

template <bool Is8Bit, bool IsStereo>
void DecodeAdpcmChunks( const uint8_t* chunks, int32_t* inOutOldSamples, int16_t* outSamples )
{
	static constexpr uint32_t SamplesPerChunk = AdpcmWordsPerChunk * ( Is8Bit ? 4 : 8 );

	for ( uint32_t i = 0; i < AdpcmChunks; ++i )
	{
		DecodeAdpcmChunk<Is8Bit, IsStereo>( chunks, inOutOldSamples, outSamples );
		outSamples += SamplesPerChunk;
		chunks += AdpcmChunkSize;
	}
}

} // namespace

void DecodeAdpcmSector( const SubHeader& subHeader, const uint8_t* data, int32_t* inOutOldSamples, int16_t* outSamples )
{
	const CodingInfo info = subHeader.codingInfo;

	if ( info.bitsPerSample == 1 )
	{
		// 8bit
		if ( info.stereo )
			DecodeAdpcmChunks<true, true>( data, inOutOldSamples, outSamples );
		else
			DecodeAdpcmChunks<true, false>( data, inOutOldSamples, outSamples );
	}
	else
	{
		// 4bit
		if ( info.stereo )
			DecodeAdpcmChunks<false, true>( data, inOutOldSamples, outSamples );
		else
			DecodeAdpcmChunks<false, false>( data, inOutOldSamples, outSamples );
	}
}

}