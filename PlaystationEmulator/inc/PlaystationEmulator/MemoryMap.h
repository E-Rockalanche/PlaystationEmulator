#pragma once

#include "BIOS.h"

#include "assert.h"

#include <array>
#include <optional>

namespace PSX
{

using Ram = Memory<2 * 1024 * 1024>;

struct Range
{
	uint32_t start;
	uint32_t size;

	constexpr bool Contains( uint32_t address ) const noexcept
	{
		return ( start <= address && address < start + size );
	}
};

class MemoryMap
{
public:
	MemoryMap( Bios& bios, Ram& ram ) : m_bios{ bios }, m_ram{ ram } {}

	template <typename T>
	T Read( uint32_t address ) const noexcept;

	template <typename T>
	void Write( uint32_t address, T value ) const noexcept;

private:
	/*
	struct Mapping
	{
		constexpr Mapping( uint32_t ramStart, uint32_t expansionStart, uint32_t scratchStart, uint32_t registersStart, uint32_t biosStart )
			: ram{ ramStart, RamSize }
			, expansion{ expansionStart, ExpansionSize }
			, scratchpad{ scratchStart, ScratchpadSize }
			, hardwareRegisters{ registersStart, HardwareRegistersSize }
			, bios{ biosStart, BiosSize }
		{}

		Range ram;
		Range expansion;
		Range scratchpad;
		Range hardwareRegisters;
		Range bios;
	};
	*/

	enum class SegmentType : uint32_t // ordered
	{
		Ram,
		Expansion,
		Scratchpad,
		HardwareRegisters,
		Bios,
		IoPorts,
		Invalid
	};

	static constexpr uint32_t RamStart = 0x00000000;
	static constexpr uint32_t RamSize = 2 * 1024 * 1024;

	static constexpr uint32_t ExpansionStart = 0x1f000000;
	static constexpr uint32_t ExpansionSize = 8 * 1024 * 1024;

	static constexpr uint32_t ScratchpadStart = 0x1f800000;
	static constexpr uint32_t ScratchpadSize = 1024;

	static constexpr uint32_t HardwareRegistersStart = 0x1f801000;
	static constexpr uint32_t HardwareRegistersSize = 8 * 1024;

	static constexpr uint32_t BiosStart = 0x1fc00000;
	static constexpr uint32_t BiosSize = 512 * 1024;

	// only in Kseg2
	static constexpr uint32_t IoStart = 0xfffe0000;
	static constexpr uint32_t IoSize = 512;

	std::pair<SegmentType, uint32_t> TranslateAddress( uint32_t address ) const noexcept;

private:
	Bios& m_bios;
	Ram& m_ram;

	/*
	const Mapping Kuseg{ 0x00000000, 0x1f000000, 0x1f800000, 0x1f801000, 0x1fc00000 };
	const Mapping Kseg0{ 0x80000000, 0x9f000000, 0x9f800000, 0x9f801000, 0x9fc00000 };
	const Mapping Kseg1{ 0xa0000000, 0xbf000000, 0xbf800000, 0xbf801000, 0xbfc00000 };
	*/

	const std::array<Range, 5> Regions
	{
		Range{ RamStart, RamSize },
		Range{ ExpansionStart, ExpansionSize },
		Range{ ScratchpadStart, ScratchpadSize },
		Range{ HardwareRegistersStart, HardwareRegistersSize },
		Range{ BiosStart, BiosSize }
	};

	// masks help strip region bits from virtual address to make a physical address
	// KSEG2 doesn't mirror the other regions so it's essentially ignored
	const std::array<uint32_t, 8> RegionMasks
	{
		// KUSEG
		0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
		// KSEG0
		0x7fffffff,
		// KSEG1
		0x1fffffff,
		// KSEG2
		0xffffffff, 0xffffffff
	};
};

template <typename T>
T MemoryMap::Read( uint32_t address ) const noexcept
{
	dbExpects( ( address % sizeof( T ) == 0 ) );

	auto[ region, offset ] = TranslateAddress( address );
	switch ( region )
	{
		case SegmentType::Bios:
			return m_bios.Read<T>( offset );

		case SegmentType::Ram:
			return m_ram.Read<T>( offset );

		case SegmentType::Expansion:
			return static_cast<T>( -1 ); // TODO

		case SegmentType::Scratchpad:
		case SegmentType::HardwareRegisters:
			// TODO
			return 0;

		case SegmentType::IoPorts:
			// TODO
			return 0;

		case SegmentType::Invalid:
			break;
	}

	dbLog( "Unhandled memory read [%X]", address );
	return 0;
}

template <typename T>
void MemoryMap::Write( uint32_t address, T value ) const noexcept
{
	dbExpects( ( address % sizeof( T ) == 0 ) );

	auto[ region, offset ] = TranslateAddress( address );
	switch ( region )
	{
		case SegmentType::Bios:
			// BIOS is read-only
			return;

		case SegmentType::Ram:
			m_ram.Write<T>( offset, value );
			return;

		case SegmentType::Expansion:
		case SegmentType::Scratchpad:
		case SegmentType::HardwareRegisters:
			// TODO
			return;

		case SegmentType::IoPorts:
			// TODO
			return;

		case SegmentType::Invalid:
			break;
	}

	dbLog( "Unhandled memory write [%X]", address );
}

}