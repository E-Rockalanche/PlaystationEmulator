#pragma once

#include "BIOS.h"
#include "CDRomDrive.h"
#include "CycleScheduler.h"
#include "InterruptControl.h"
#include "DMA.h"
#include "GPU.h"
#include "MemoryControl.h"
#include "PeripheralPorts.h"
#include "RAM.h"
#include "Timers.h"

#include <stdx/assert.h>

#include <array>
#include <optional>

namespace PSX
{

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
	MemoryMap(
		Ram& ram,
		Scratchpad& scratchpad,
		MemoryControl& memControl,
		PeripheralPorts& peripheralPorts,
		InterruptControl& interrupts,
		Dma& dma,
		Timers& timers,
		CDRomDrive& cdRomDrive,
		Gpu& gpu,
		Bios& bios,
		CycleScheduler& cycleScheduler )
		: m_ram{ ram }
		, m_scratchpad{ scratchpad }
		, m_memoryControl{ memControl }
		, m_peripheralPorts{ peripheralPorts }
		, m_interruptControl{ interrupts }
		, m_dma{ dma }
		, m_timers{ timers }
		, m_cdRomDrive{ cdRomDrive }
		, m_gpu{ gpu }
		, m_bios{ bios }
		, m_cycleScheduler{ cycleScheduler }
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
	static constexpr Range PeripheralPortRange{ 0x1f801040, 0x20 };
	static constexpr Range RamSizeRange{ 0x1f801060, 4 };
	static constexpr Range InterruptControlRange{ 0x1f801070, 8 };
	static constexpr Range DmaChannelsRange{ 0x1f801080, 128 };
	static constexpr Range TimersRange{ 0x1f801100, 48 };
	static constexpr Range CDRomRange{ 0x1f801800, 4 };
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
		PeripheralPorts,
		RamSize,
		InterruptControl,
		DmaChannels,
		Timers,
		CDRomDrive,
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
		PeripheralPortRange,
		RamSizeRange,
		InterruptControlRange,
		DmaChannelsRange,
		TimersRange,
		CDRomRange,
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
	PeripheralPorts& m_peripheralPorts;
	InterruptControl& m_interruptControl;
	Dma& m_dma;
	Timers& m_timers;
	CDRomDrive& m_cdRomDrive;
	Gpu& m_gpu;
	Bios& m_bios;

	CycleScheduler& m_cycleScheduler;
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
			// dbLog( "read from expansion 1 [%X]", address );
			return static_cast<T>( -1 ); // send all ones for missing expansion

		case Segment::Scratchpad:
			// TODO: not mirrored in KSEG1
			return m_scratchpad.Read<T>( offset );

		case Segment::MemControl:
			return static_cast<T>( m_memoryControl.Read( offset / 4 ) );

		case Segment::PeripheralPorts:
			return static_cast<T>( m_peripheralPorts.Read( offset/ 4 ) );

		case Segment::RamSize:
			return static_cast<T>( m_memoryControl.ReadRamSize() );

		case Segment::InterruptControl:
			return static_cast<T>( m_interruptControl.Read( offset / 4 ) );

		case Segment::DmaChannels:
			return static_cast<T>( m_dma.Read( offset / 4 ) );

		case Segment::Timers:
			m_cycleScheduler.UpdateNow();
			return static_cast<T>( m_timers.Read( offset ) );

		case Segment::CDRomDrive:
		{
			if ( offset == 2 )
				return m_cdRomDrive.ReadDataFifo<T>();
			else
				return m_cdRomDrive.Read( offset );
		}

		case Segment::Gpu:
		{
			switch ( offset / 4 )
			{
				case 0: return static_cast<T>( m_gpu.GpuRead() );
				case 1: return static_cast<T>( m_gpu.GpuStatus() );
				default: dbBreak(); return 0;
			}
			break;
		}

		case Segment::Spu:
			// dbLog( "read SPU register [%X]", address );
			return 0; // TODO

		case Segment::Expansion2:
			// dbLog( "read from expansion 2 [%X]", address );
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
			// dbLog( "write to expansion 1 [%X <- %X]", address, value );
			break; // ignore writes to missing expansion

		case Segment::Scratchpad:
			// TODO: not mirrored in KSEG1
			m_scratchpad.Write<T>( offset, value );
			break;

		case Segment::MemControl:
			m_memoryControl.Write( offset / 4, ShiftValueForRegister<uint32_t>( offset, value ) );
			break;

		case Segment::PeripheralPorts:
			m_peripheralPorts.Write( offset / 4, ShiftValueForRegister<uint32_t>( offset, value ) );
			break;

		case Segment::RamSize:
			m_memoryControl.WriteRamSize( ShiftValueForRegister<uint32_t>( offset, value ) );
			break;

		case Segment::InterruptControl:
			m_interruptControl.Write( offset / 4, value );
			break;

		case Segment::DmaChannels:
			m_dma.Write( offset / 4, ShiftValueForRegister<uint32_t>( offset, value ) );
			break;

		case Segment::Timers:
			m_cycleScheduler.UpdateNow();
			m_timers.Write( offset, value );
			break;

		case Segment::CDRomDrive:
			m_cdRomDrive.Write( offset, static_cast<uint8_t>( value ) );
			break;

		case Segment::Gpu:
		{
			const uint32_t shifted = ShiftValueForRegister<uint32_t>( offset, value );
			switch ( offset / 4 )
			{
				case 0: m_gpu.WriteGP0( shifted );	break;
				case 1: m_gpu.WriteGP1( shifted );	break;
				default: dbBreak(); break;
			}
			break;
		}

		case Segment::Spu:
			// dbLog( "write to SPU register [%X <- %X]", address, value );
			break;

		case Segment::Expansion2:
			// dbLog( "write to expansion 2 [%X <- %X]", address, value );
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