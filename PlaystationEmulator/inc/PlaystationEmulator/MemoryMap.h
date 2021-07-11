#pragma once

#include "BIOS.h"
#include "CDRomDrive.h"
#include "CycleScheduler.h"
#include "InterruptControl.h"
#include "DMA.h"
#include "GPU.h"
#include "MemoryControl.h"
#include "ControllerPorts.h"
#include "RAM.h"
#include "Timers.h"

#include <stdx/assert.h>

#include <array>
#include <optional>

namespace PSX
{

class MemoryMap
{
public:
	MemoryMap(
		Ram& ram,
		Scratchpad& scratchpad,
		MemoryControl& memControl,
		ControllerPorts& peripheralPorts,
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
		, m_controllerPorts{ peripheralPorts }
		, m_interruptControl{ interrupts }
		, m_dma{ dma }
		, m_timers{ timers }
		, m_cdRomDrive{ cdRomDrive }
		, m_gpu{ gpu }
		, m_bios{ bios }
		, m_cycleScheduler{ cycleScheduler }
	{}

	template <typename T>
	T Read( uint32_t address ) const noexcept
	{
		T value;
		Access<T, true>( address, value );
		return value;
	}

	template <typename T>
	void Write( uint32_t address, T value ) const noexcept
	{
		Access<T, false>( address, value );
	}

private:

	enum : uint32_t
	{
		RamStart = 0x00000000,
		RamSize = 2 * 1024 * 1024,
		RamMirrorSize = 8 * 1024 * 1024,

		Expansion1Start = 0x1f000000,
		Expansion1Size = 8 * 1024 * 1024,

		ScratchpadStart = 0x1f800000,
		ScratchpadSize = 1024,

		MemControlStart = 0x1f801000,
		MemControlSize = 0x24,
		MemControlRamStart = 0x1f801060,
		MemControlRamSize = 4,

		ControllerStart = 0x1f801040,
		ControllerSize = 0x20,

		InterruptControlStart = 0x1f801070,
		InterruptControlSize = 8,

		DmaStart = 0x1f801080,
		DmaSize = 128,

		TimersStart = 0x1f801100,
		TimersSize = 48,

		CdRomStart = 0x1f801800,
		CdRomSize = 4,

		GpuStart = 0x1f801810,
		GpuSize = 8,

		SpuStart = 0x1f801c00,
		SpuSize = 1024,

		Expansion2Start = 0x1f802000,
		Expansion2Size = 128,

		BiosStart = 0x1fc00000,
		BiosSize = 512 * 1024,

		CacheControlStart = 0xfffe0130,
		CacheControlSize = 4
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

	template <typename T, bool Read>
	void Access( uint32_t address, T& value ) const noexcept;

	// shift value if writing to register with non-aligned address
	template <typename RegType, typename T>
	static inline constexpr RegType ShiftValueForRegister( T value, uint32_t address ) noexcept
	{
		return static_cast<RegType>( value ) << ( address & ( sizeof( RegType ) - 1 ) ) * 8;
	}

	template <typename T, bool Read, typename MemoryType>
	inline void AccessMemory( MemoryType& memory, uint32_t offset, T& value ) const noexcept
	{
		if constexpr ( Read )
			value = memory.Read<T>( offset );
		else
			memory.Write<T>( offset, value );
	}

	template <typename T, bool Read, typename Component>
	inline void AccessComponent32( Component& component, uint32_t offset, T& value ) const noexcept
	{
		if constexpr ( Read )
			value = static_cast<T>( component.Read( offset / 4 ) );
		else
			component.Write( offset / 4, ShiftValueForRegister<uint32_t>( value, offset ) );
	}

	template <typename T, bool Read>
	void AccessControllerPort( uint32_t offset, T& value ) const noexcept;

	static inline constexpr bool Within( uint32_t address, uint32_t start, uint32_t size ) noexcept
	{
		return ( start <= address && address < start + size );
	}

private:
	Ram& m_ram;
	Scratchpad& m_scratchpad;
	MemoryControl& m_memoryControl;
	ControllerPorts& m_controllerPorts;
	InterruptControl& m_interruptControl;
	Dma& m_dma;
	Timers& m_timers;
	CDRomDrive& m_cdRomDrive;
	Gpu& m_gpu;
	Bios& m_bios;

	CycleScheduler& m_cycleScheduler;
};

template <typename T, bool Read>
void MemoryMap::Access( uint32_t address, T& value ) const noexcept
{
	// upper 3 bits determine segment
	// convert virtual address to physical address
	address &= RegionMasks[ address >> 29 ];

	if ( address <= RamMirrorSize ) // ram starts at 0
	{
		AccessMemory<T, Read>( m_ram, address & ( RamSize - 1 ), value );
	}
	else if ( Within( address, BiosStart, BiosSize ) )
	{
		// read only
		if constexpr ( Read )
			value = m_bios.Read<T>( address - BiosStart );
	}
	else if ( Within( address, ScratchpadStart, ScratchpadSize ) )
	{
		AccessMemory<T, Read>( m_scratchpad, address - ScratchpadStart, value );
	}
	else if ( Within( address, MemControlStart, MemControlSize ) )
	{
		AccessComponent32<T, Read>( m_memoryControl, address - MemControlStart, value );
	}
	else if ( Within( address, ControllerStart, ControllerSize ) )
	{
		AccessControllerPort<T, Read>( address - ControllerStart, value );
	}
	else if ( Within( address, MemControlRamStart, MemControlRamSize ) )
	{
		if constexpr ( Read )
			value = static_cast<T>( m_memoryControl.ReadRamSize() );
		else
			m_memoryControl.WriteRamSize( ShiftValueForRegister<uint32_t>( value, address ) );
	}
	else if ( Within( address, InterruptControlStart, InterruptControlSize ) )
	{
		AccessComponent32<T, Read>( m_interruptControl, address - InterruptControlStart, value );
	}
	else if ( Within( address, DmaStart, DmaSize ) )
	{
		AccessComponent32<T, Read>( m_dma, address - DmaStart, value );
	}
	else if ( Within( address, TimersStart, TimersSize ) )
	{
		m_cycleScheduler.UpdateNow();
		AccessComponent32<T, Read>( m_timers, address - TimersStart, value );
	}
	else if ( Within( address, CdRomStart, CdRomSize ) )
	{
		if constexpr ( Read )
			value = m_cdRomDrive.Read( address - CdRomStart );
		else
			m_cdRomDrive.Write( address - CdRomStart, static_cast<uint8_t>( value ) );
	}
	else if ( Within( address, GpuStart, GpuSize ) )
	{
		AccessComponent32<T, Read>( m_gpu, address - GpuStart, value );
	}
	else if ( Within( address, SpuStart, SpuSize ) )
	{
		// TODO
	}
	else if ( Within( address, CacheControlStart, CacheControlSize ) )
	{
		if constexpr ( Read )
			value = static_cast<T>( m_memoryControl.ReadCacheControl() );
		else
			m_memoryControl.WriteCacheControl( ShiftValueForRegister<uint32_t>( value, address ) );
	}
	else if ( Within( address, Expansion1Start, Expansion1Size ) )
	{
		// TODO
		if constexpr ( Read )
			value = 0;
	}
	else if ( Within( address, Expansion2Start, Expansion2Size ) )
	{
		if constexpr ( Read )
			value = static_cast<T>( -1 );
	}
	else
	{
		if constexpr ( Read )
			dbBreakMessage( "Unhandled memory read [%X]", address );
		else
			dbBreakMessage( "Unhandled memory write [%X <- %X]", address, value );
	}
}

template <typename T, bool Read>
void MemoryMap::AccessControllerPort( uint32_t offset, T& value ) const noexcept
{
	if constexpr ( Read )
	{
		switch ( offset / 2 )
		{
			// 32bit registers
			case 0:
			case 1:	value = static_cast<T>( m_controllerPorts.ReadData() );					break;
			case 2:
			case 3:	value = static_cast<T>( m_controllerPorts.ReadStatus() );				break;

			// 16bit registers
			case 4:	value = static_cast<T>( m_controllerPorts.ReadMode() );					break;
			case 5:	value = static_cast<T>( m_controllerPorts.ReadControl() );				break;
			case 6:	value = 0;																break;
			case 7:	value = static_cast<T>( m_controllerPorts.ReadBaudrateReloadValue() );	break;
		}
	}
	else
	{
		switch ( offset / 2 )
		{
			// 32bit registers
			case 0:
			case 1:	m_controllerPorts.WriteData( ShiftValueForRegister<uint32_t>( value, offset ) );				break;
			case 2:
			case 3:																									break; // status is read-only

			// 16bit registers
			case 4:	m_controllerPorts.WriteMode( ShiftValueForRegister<uint16_t>( value, offset ) );				break;
			case 5:	m_controllerPorts.WriteControl( ShiftValueForRegister<uint16_t>( value, offset ) );				break;
			case 6:																									break;
			case 7:	m_controllerPorts.WriteBaudrateReloadValue( ShiftValueForRegister<uint16_t>( value, offset ) );	break;
		}
	}
}

}