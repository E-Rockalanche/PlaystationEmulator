#pragma once

#include "BIOS.h"
#include "MemoryControl.h"

#include "assert.h"

#include <array>
#include <optional>

namespace PSX
{

using Ram = Memory<2 * 1024 * 1024>;
using Scratchpad = Memory<1024>;

struct Range
{
	constexpr Range( uint32_t start_, uint32_t size_ ) noexcept : start{ start_ }, size{ size_ } {}

	constexpr std::optional<uint32_t> Contains( uint32_t address ) const noexcept
	{
		return ( start <= address && address < start + size ) ? std::optional{ address - start } : std::nullopt;
	}

	uint32_t start;
	uint32_t size;
};

class MemoryMap
{
public:
	MemoryMap( Ram& ram, Scratchpad& scratchpad, MemoryControl& memControl, Bios& bios )
		: m_ram{ ram }
		, m_scratchpad{ scratchpad }
		, m_memoryControl{ memControl }
		, m_bios{ bios }
	{}

	template <typename T>
	T Read( uint32_t address ) const noexcept;

	template <typename T>
	void Write( uint32_t address, T value ) const noexcept;

private:
	static constexpr Range RamRange{ 0x00000000, Ram::Size() };
	static constexpr Range ExpansionRange{ 0x1f000000, 8 * 1024 * 1024 };
	static constexpr Range ScratchpadRange{ 0x1f800000, Scratchpad::Size() };
	static constexpr Range MemControlRange{ 0x1f801000, 0x24 };
	static constexpr Range RamSizeRange{ 0x1f801060, 4 };
	static constexpr Range InterruptControlRange{ 0x1f801070, 8 };
	static constexpr Range DmaChannelsRange{ 0x1f801080, 128 };
	static constexpr Range TimersRange{ 0x1f801100, 48 };
	static constexpr Range GpuRange{ 0x1f801810, 8 };
	static constexpr Range SpuRange{ 0x1f801c00, 1024 };
	static constexpr Range ExpansionRange2{ 0x1f802000, 128 };
	static constexpr Range BiosRange{ 0x1fc00000, 512 * 1024 };
	static constexpr Range CacheControlRange{ 0xfffe0130, 4 };

	enum class Segment
	{
		Invalid = -1,

		Ram = 0,
		Expansion,
		Scratchpad,
		MemControl,
		RamSize,
		InterruptControl,
		DmaChannels,
		Timers,
		Gpu,
		Spu,
		Expansion2,
		Bios,
		CacheControl,

		Count
	};

	static constexpr std::array<Range, static_cast<size_t>( Segment::Count )> Segments
	{
		RamRange,
		ExpansionRange,
		ScratchpadRange,
		MemControlRange,
		RamSizeRange,
		InterruptControlRange,
		DmaChannelsRange,
		TimersRange,
		GpuRange,
		SpuRange,
		ExpansionRange2,
		BiosRange,
		CacheControlRange
	};

	// masks help strip region bits from virtual address to make a physical address
	// KSEG2 doesn't mirror the other regions so it's essentially ignored
	static constexpr std::array<uint32_t, 8> RegionMasks
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

	std::pair<MemoryMap::Segment, uint32_t> TranslateAddress( uint32_t address ) const noexcept;
	
	// shift value if writing to register with non-aligned address
	template <typename RegType, typename T>
	static constexpr RegType ShiftValueForRegister( uint32_t address, T value ) noexcept
	{
		return static_cast<RegType>( value ) << ( address & ( sizeof( RegType ) - 1 ) ) * 8;
	}

private:
	Ram& m_ram;
	Scratchpad& m_scratchpad;
	MemoryControl& m_memoryControl;
	Bios& m_bios;
};

template <typename T>
T MemoryMap::Read( uint32_t address ) const noexcept
{
	dbExpects( ( address % sizeof( T ) == 0 ) );

	const auto[ region, offset ] = TranslateAddress( address );
	switch ( region )
	{
		case Segment::Ram:
			// TODO: RAM can be mirrored to first 8MB (enabled by default)
			return m_ram.Read<T>( offset );

		case Segment::Expansion:
			dbLog( "read from expansion 1 [%X]", address );
			return static_cast<T>( -1 ); // send all ones for missing expansion

		case Segment::Scratchpad:
			// TODO: not mirrored in KSEG1
			return m_scratchpad.Read<T>( offset );

		case Segment::MemControl:
			return static_cast<T>( m_memoryControl.Read( offset / 4 ) );

		case Segment::RamSize:
			return static_cast<T>( m_memoryControl.ReadRamSize() );

		case Segment::InterruptControl:
			dbLog( "read from interrupt control [%X]", address );
			return 0; // TODO

		case Segment::DmaChannels:
			dbLog( "read from DMA channel [%X]", address );
			return 0; // TODO

		case Segment::Timers:
			dbLog( "read from timer [%X]", address );
			return 0; // TODO

		case Segment::Gpu:
			dbLog( "read from gpu [%X]", address );
			return static_cast<T>( 0x10000000 ); // TODO

		case Segment::Spu:
			dbLog( "read SPU register [%X]", address );
			return 0; // TODO

		case Segment::Expansion2:
			dbLog( "read from expansion 2 [%X]", address );
			return static_cast<T>( -1 ); // send all ones for missing expansion

		case Segment::Bios:
			// TODO: bios can be mirrored to last 4MB (disabled by default)
			return m_bios.Read<T>( offset );

		case Segment::CacheControl:
			return static_cast<T>( m_memoryControl.ReadCacheControl() );

		default:
			dbBreakMessage( "Unhandled memory read [%X]", address );
			return 0;
	}
}

template <typename T>
void MemoryMap::Write( uint32_t address, T value ) const noexcept
{
	dbExpects( ( address % sizeof( T ) == 0 ) );

	auto[ region, offset ] = TranslateAddress( address );
	switch ( region )
	{
		case Segment::Bios:
			// BIOS is read-only
			dbBreakMessage( "write to BIOS [%X <- %X]", address, value );
			break;

		case Segment::Ram:
			m_ram.Write<T>( offset, value );
			break;

		case Segment::Expansion:
			dbLog( "write to expansion 1 [%X <- %X]", address, value );
			break; // ignore writes to missing expansion

		case Segment::Scratchpad:
			// TODO: not mirrored in KSEG1
			m_scratchpad.Write<T>( offset, value );
			break;

		case Segment::MemControl:
			m_memoryControl.Write( offset / 4, ShiftValueForRegister<uint32_t>( offset, value ) );
			break;

		case Segment::RamSize:
			m_memoryControl.WriteRamSize( ShiftValueForRegister<uint32_t>( offset, value ) );
			break;

		case Segment::InterruptControl:
			dbLog( "write to interrupt control [%X <- %X]", address, value );
			break; // TODO

		case Segment::DmaChannels:
			dbLog( "write to DMA channel [%X <- %X]", address, value );
			break; // TODO

		case Segment::Timers:
			dbLog( "write to timer [%X <- %X]", address, value );
			break; // TODO

		case Segment::Gpu:
			dbLog( "write to gpu [%X <- %X]", address, value );
			break;

		case Segment::Spu:
			dbLog( "write to SPU register [%X <- %X]", address, value );
			break;

		case Segment::Expansion2:
			dbLog( "write to expansion 2 [%X <- %X]", address, value );
			break;

		case Segment::CacheControl:
			m_memoryControl.WriteCacheControl( ShiftValueForRegister<uint32_t>( offset, value ) );
			break;

		default:
			dbBreakMessage( "Unhandled memory write [%X <- %X]", address, value );
			break;
	}
}

}