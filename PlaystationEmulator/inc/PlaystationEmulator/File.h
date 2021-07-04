#pragma once

#include <cstdint>
#include <string_view>

namespace PSX
{

struct ExeHeader
{
	static constexpr std::string_view Id = "PS-X EXE";

	char id[ 0x10 ]; // "PS-X EXE"

	uint32_t programCounter;
	uint32_t globalPointer; // register 28

	uint32_t ramDestination;
	uint32_t fileSize;

	uint32_t unknown1;
	uint32_t unknown2;

	uint32_t memfillStart;
	uint32_t memfillSize;

	uint32_t stackPointerBase;
	uint32_t stackPointerOffset; // added to base

	// reserved for a function (should be zero filled )
	// marker (ignored)
	// zero filled to 0x7ff
	char zeroFilled[ 0x7C8 ];
};
static_assert( sizeof( ExeHeader ) == 0x800 );

}