#pragma once

#include "BIOS.h"
#include "CDRomDrive.h"
#include "ControllerPorts.h"
#include "DMA.h"
#include "DualSerialPort.h"
#include "InterruptControl.h"
#include "GPU.h"
#include "MacroblockDecoder.h"
#include "MemoryControl.h"
#include "RAM.h"
#include "SPU.h"
#include "Timers.h"

#include <stdx/assert.h>

#include <array>
#include <optional>

namespace PSX
{

class MemoryMap
{
public:
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

		MdecStart = 0x1F801820,
		MdecSize = 8,

		SpuStart = 0x1f801c00,
		SpuSize = 1024,

		Expansion2Start = 0x1f802000,
		Expansion2Size = 128,

		BiosStart = 0x1fc00000,
		BiosSize = 512 * 1024,

		CacheControlStart = 0xfffe0130,
		CacheControlSize = 4
	};

public:
	MemoryMap(
		Bios& bios,
		CDRomDrive& cdRomDrive,
		ControllerPorts& controllerPorts,
		Dma& dma,
		Gpu& gpu,
		InterruptControl& interruptControl,
		MacroblockDecoder& mdec,
		MemoryControl& memControl,
		Ram& ram,
		Scratchpad& scratchpad,
		Spu& spu,
		Timers& timers )
		: m_bios{ bios }
		, m_cdRomDrive{ cdRomDrive }
		, m_controllerPorts{ controllerPorts }
		, m_dma{ dma }
		, m_gpu{ gpu }
		, m_interruptControl{ interruptControl }
		, m_mdec{ mdec }
		, m_memoryControl{ memControl }
		, m_ram{ ram }
		, m_scratchpad{ scratchpad }
		, m_spu{ spu }
		, m_timers{ timers }
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

	void SetDualSerialPort( DualSerialPort* dualSerialPort ) noexcept
	{
		m_dualSerialPort = dualSerialPort;
	}

private:
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

private:
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

	template <typename T, bool Read>
	void AccessSpu( uint32_t offset, T& value ) const noexcept;

	static inline constexpr bool Within( uint32_t address, uint32_t start, uint32_t size ) noexcept
	{
		return ( start <= address && address < start + size );
	}

private:

	Bios& m_bios;
	CDRomDrive& m_cdRomDrive;
	ControllerPorts& m_controllerPorts;
	Dma& m_dma;
	Gpu& m_gpu;
	InterruptControl& m_interruptControl;
	MacroblockDecoder& m_mdec;
	MemoryControl& m_memoryControl;
	Ram& m_ram;
	Scratchpad& m_scratchpad;
	Spu& m_spu;
	Timers& m_timers;

	DualSerialPort* m_dualSerialPort = nullptr;
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
		AccessComponent32<T, Read>( m_timers, address - TimersStart, value );
	}
	else if ( Within( address, CdRomStart, CdRomSize ) )
	{
		if constexpr ( Read )
		{
			const uint32_t offset = address - CdRomStart;
			if ( offset == 2 )
				value = m_cdRomDrive.ReadDataFifo<T>();
			else
				value = m_cdRomDrive.Read( offset );
		}
		else
		{
			m_cdRomDrive.Write( address - CdRomStart, static_cast<uint8_t>( value ) );
		}
	}
	else if ( Within( address, GpuStart, GpuSize ) )
	{
		AccessComponent32<T, Read>( m_gpu, address - GpuStart, value );
	}
	else if ( Within( address, MdecStart, MdecSize ) )
	{
		AccessComponent32<T, Read>( m_mdec, address - MdecStart, value );
	}
	else if ( Within( address, SpuStart, SpuSize ) )
	{
		AccessSpu<T, Read>( address - SpuStart, value );
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
			value = T( -1 );
	}
	else if ( Within( address, Expansion2Start, Expansion2Size ) )
	{
		if constexpr ( Read )
			value = m_dualSerialPort
				? static_cast<T>( m_dualSerialPort->Read( address - Expansion2Start ) )
				: T( -1 );
		else if ( m_dualSerialPort )
			m_dualSerialPort->Write( address - Expansion2Start, static_cast<uint8_t>( value ) );
	}
	else
	{
		if constexpr ( Read )
		{
			dbBreakMessage( "Unhandled memory read [%X]", address );
			value = T( -1 );
		}
		else
		{
			dbBreakMessage( "Unhandled memory write [%X <- %X]", address, value );
		}
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
			case 6:	value = T( -1 );														break;
			case 7:	value = static_cast<T>( m_controllerPorts.ReadBaudrateReloadValue() );	break;

			default:
				dbBreak();
				value = static_cast<T>( -1 );
				break;
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

			default:
				dbBreak();
				break;
		}
	}
}

template <typename T, bool Read>
void MemoryMap::AccessSpu( uint32_t offset, T& value ) const noexcept
{
	dbExpects( offset % 2 == 0 );

	if constexpr ( Read )
	{
		if constexpr ( sizeof( T ) == 4 )
		{
			const uint32_t low = m_spu.Read( offset );
			const uint32_t high = m_spu.Read( offset + 2 );
			value = static_cast<T>( low | ( high << 16 ) );
		}
		else
		{
			value = static_cast<T>( m_spu.Read( offset ) );
		}
	}
	else
	{
		if constexpr ( sizeof( T ) == 4 )
		{
			m_spu.Write( offset, static_cast<uint16_t>( value ) );
			m_spu.Write( offset + 2, static_cast<uint16_t>( value >> 16 ) );
		}
		else
		{
			m_spu.Write( offset, static_cast<uint16_t>( value ) );
		}
	}


		
}

}