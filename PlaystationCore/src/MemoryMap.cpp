#include "MemoryMap.h"

#include "BIOS.h"
#include "CDRomDrive.h"
#include "ControllerPorts.h"
#include "DMA.h"
#include "DualSerialPort.h"
#include "EventManager.h"
#include "GPU.h"
#include "Instruction.h"
#include "InterruptControl.h"
#include "MacroblockDecoder.h"
#include "Memory.h"
#include "MemoryControl.h"
#include "RAM.h"
#include "SPU.h"
#include "Timers.h"

namespace PSX
{

namespace
{

inline constexpr bool Within( uint32_t address, uint32_t start, uint32_t size ) noexcept
{
	return ( start <= address && address < start + size );
}

}

template <typename T, bool Read>
void MemoryMap::Access( uint32_t address, T& value ) const noexcept
{
	static_assert( std::is_unsigned_v<T> ); // don't want to duplicate Access function for signed and unsigned types

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
	else if ( Within( address, SerialPortStart, SerialPortSize ) )
	{
		if constexpr ( Read )
			value = T( -1 );
	}
	else if ( Within( address, MemControlRamStart, MemControlRamSize ) )
	{
		if constexpr ( Read )
			value = ShiftValueForRead<T>( m_memoryControl.ReadRamSize(), address );
		else
			m_memoryControl.WriteRamSize( ShiftValueForWrite<uint32_t>( value, address ) );
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
			{
				value = m_cdRomDrive.Read( 2 );
				if constexpr ( sizeof( T ) >= 2 )
					value |= static_cast<T>( m_cdRomDrive.Read( 2 ) << 8 );
			}
			else
			{
				value = m_cdRomDrive.Read( offset );
			}
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
			m_memoryControl.WriteCacheControl( ShiftValueForWrite<uint32_t>( value, address ) );
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
			dbBreakMessage( "Unhandled memory write [%X <= %X]", address, value );
		}
	}
}

// force template instantiations for unsigned read/write access

template
void MemoryMap::Access<uint8_t, true>( uint32_t address, uint8_t& value ) const noexcept;

template
void MemoryMap::Access<uint8_t, false>( uint32_t address, uint8_t& value ) const noexcept;

template
void MemoryMap::Access<uint16_t, true>( uint32_t address, uint16_t& value ) const noexcept;

template
void MemoryMap::Access<uint16_t, false>( uint32_t address, uint16_t& value ) const noexcept;

template
void MemoryMap::Access<uint32_t, true>( uint32_t address, uint32_t& value ) const noexcept;

template
void MemoryMap::Access<uint32_t, false>( uint32_t address, uint32_t& value ) const noexcept;

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
			case 1:	m_controllerPorts.WriteData( ShiftValueForWrite<uint32_t>( value, offset ) );					break;
			case 2:
			case 3:																									break; // status is read-only

			// 16bit registers
			case 4:	m_controllerPorts.WriteMode( ShiftValueForWrite<uint16_t>( value, offset ) );					break;
			case 5:	m_controllerPorts.WriteControl( ShiftValueForWrite<uint16_t>( value, offset ) );				break;
			case 6:																									break;
			case 7:	m_controllerPorts.WriteBaudrateReloadValue( ShiftValueForWrite<uint16_t>( value, offset ) );	break;

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
			const uint32_t low = m_spu.Read( offset / 2 );
			const uint32_t high = m_spu.Read( offset / 2 + 1 );
			value = static_cast<T>( low | ( high << 16 ) );
		}
		else
		{
			value = static_cast<T>( m_spu.Read( offset / 2 ) );
		}
	}
	else
	{
		if constexpr ( sizeof( T ) == 4 )
		{
			m_spu.Write( offset / 2, static_cast<uint16_t>( value ) );
			m_spu.Write( offset / 2 + 1, static_cast<uint16_t>( value >> 16 ) );
		}
		else
		{
			m_spu.Write( offset / 2, static_cast<uint16_t>( value ) );
		}
	}
}

bool MemoryMap::CheckAndPrefetchICache( uint32_t address ) noexcept
{
	dbExpects( address % 4 == 0 ); // instructions must be word-aligned

	const uint32_t index = ( address >> 2 ) & 0x3u;
	const uint32_t line = ( address >> 4 ) & 0xffu;
	const uint32_t tag = ( address >> 12 );

	auto& flags = m_icacheFlags[ line ];

	const bool cached = ( flags.tag == tag ) && ( flags.valid & ( 1u << index ) );

	if ( !cached )
	{
		flags.tag = tag;
		flags.valid = ( 0x3u << index ) & 0x3u;

		// TODO: pre-fetch next instructions in 4-word block
	}

	return cached;
}

std::optional<Instruction> MemoryMap::FetchInstruction( uint32_t address ) noexcept
{
	dbExpects( address % 4 == 0 );
	
	// TODO: icaching

	address &= 0x1fffffff;

	if ( address < RamMirrorSize )
	{
		return Instruction( m_ram.Read<uint32_t>( address % RamSize ) );
	}
	else if ( Within( address, BiosStart, BiosSize ) )
	{
		return Instruction( m_bios.Read<uint32_t>( address - BiosStart ) );
	}
	else
	{
		return std::nullopt;
	}
}

const uint8_t* MemoryMap::GetRealAddress( uint32_t address ) const noexcept
{
	address &= 0x1fffffff;

	if ( address < RamMirrorSize )
	{
		return m_ram.Data() + ( address % RamSize );
	}
	else if ( Within( address, BiosStart, BiosSize ) )
	{
		return m_bios.Data() + ( address - BiosStart );
	}
	else if ( Within( address, ScratchpadStart, ScratchpadSize ) )
	{
		return m_scratchpad.Data() + ( address - ScratchpadStart );
	}
	else
	{
		return nullptr;
	}
}

}