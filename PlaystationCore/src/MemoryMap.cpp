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
#include "SerialPort.h"
#include "SPU.h"
#include "SaveState.h"
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

void MemoryMap::Reset()
{
	m_memoryControl.Reset();
	m_icacheFlags.fill( ICacheFlags() );
}

template <typename T, bool ReadMode>
void MemoryMap::Access( uint32_t address, T& value ) noexcept
{
	static_assert( std::is_unsigned_v<T> ); // don't want to duplicate Access function for signed and unsigned types

	cycles_t cycles = ReadMode ? DeviceReadCycles : 0;

	// upper 3 bits determine segment
	// convert virtual address to physical address
	address &= RegionMasks[ address >> 29 ];

	if ( address <= RamMirrorSize ) // ram starts at 0
	{
		AccessMemory<T, ReadMode>( m_ram, address % RamSize, value );
		if constexpr ( ReadMode )
			cycles = RamReadCycles;
	}
	else if ( Within( address, BiosStart, BiosSize ) )
	{
		// read only
		if constexpr ( ReadMode )
		{
			value = m_bios.Read<T>( address - BiosStart );
			cycles = m_memoryControl.GetAccessCycles<T>( MemoryControl::DelaySizeType::Bios );
		}
	}
	else if ( Within( address, ScratchpadStart, ScratchpadSize ) )
	{
		AccessMemory<T, ReadMode>( m_scratchpad, address - ScratchpadStart, value );
		cycles = 1;
	}
	else if ( Within( address, MemControlStart, MemControlSize ) )
	{
		AccessComponent32<T, ReadMode>( m_memoryControl, address - MemControlStart, value );
	}
	else if ( Within( address, ControllerStart, ControllerSize ) )
	{
		AccessControllerPort<T, ReadMode>( address - ControllerStart, value );
	}
	else if ( Within( address, SerialPortStart, SerialPortSize ) )
	{
		AccessSerialPort<T, ReadMode>( address - SerialPortStart, value );
	}
	else if ( Within( address, MemControlRamStart, MemControlRamSize ) )
	{
		if constexpr ( ReadMode )
			value = ShiftValueForRead<T>( m_memoryControl.ReadRamSize(), address );
		else
			m_memoryControl.WriteRamSize( ShiftValueForWrite<uint32_t>( value, address ) );
	}
	else if ( Within( address, InterruptControlStart, InterruptControlSize ) )
	{
		AccessComponent32<T, ReadMode>( m_interruptControl, address - InterruptControlStart, value );
	}
	else if ( Within( address, DmaStart, DmaSize ) )
	{
		AccessComponent32<T, ReadMode>( m_dma, address - DmaStart, value );
	}
	else if ( Within( address, TimersStart, TimersSize ) )
	{
		AccessComponent32<T, ReadMode>( m_timers, address - TimersStart, value );
	}
	else if ( Within( address, CdRomStart, CdRomSize ) )
	{
		AccessCDRomDrive<T, ReadMode>( address - CdRomStart, value );

		if constexpr ( ReadMode )
			cycles = m_memoryControl.GetAccessCycles<T>( MemoryControl::DelaySizeType::CDRom );
	}
	else if ( Within( address, GpuStart, GpuSize ) )
	{
		AccessComponent32<T, ReadMode>( m_gpu, address - GpuStart, value );
	}
	else if ( Within( address, MdecStart, MdecSize ) )
	{
		AccessComponent32<T, ReadMode>( m_mdec, address - MdecStart, value );
	}
	else if ( Within( address, SpuStart, SpuSize ) )
	{
		AccessSpu<T, ReadMode>( address - SpuStart, value );
		if constexpr ( ReadMode )
			cycles = m_memoryControl.GetAccessCycles<T>( MemoryControl::DelaySizeType::Spu );
	}
	else if ( Within( address, CacheControlStart, CacheControlSize ) )
	{
		if constexpr ( ReadMode )
		{
			value = ShiftValueForRead<T>( m_memoryControl.ReadCacheControl(), address );
			cycles = 1;
		}
		else
		{
			m_memoryControl.WriteCacheControl( ShiftValueForWrite<uint32_t>( value, address ) );
		}
	}
	else if ( Within( address, Expansion1Start, Expansion1Size ) )
	{
		// TODO
		if constexpr ( ReadMode )
		{
			value = T( -1 );
			cycles = m_memoryControl.GetAccessCycles<T>( MemoryControl::DelaySizeType::Expansion1 );
		}
	}
	else if ( Within( address, Expansion2Start, Expansion2Size ) )
	{
		if constexpr ( ReadMode )
		{
			value = m_dualSerialPort
				? static_cast<T>( m_dualSerialPort->Read( address - Expansion2Start ) )
				: T( -1 );

			cycles = m_memoryControl.GetAccessCycles<T>( MemoryControl::DelaySizeType::Expansion2 );
		}
		else if ( m_dualSerialPort )
		{
			m_dualSerialPort->Write( address - Expansion2Start, static_cast<uint8_t>( value ) );
		}
	}
	else if ( Within( address, Expansion3Start, Expansion3Size ) )
	{
		if constexpr ( ReadMode )
		{
			value = T( -1 );
			cycles = m_memoryControl.GetAccessCycles<T>( MemoryControl::DelaySizeType::Expansion3 );
		}
	}
	else
	{
		if constexpr ( ReadMode )
		{
			dbLogWarning( "Unhandled memory read [%X]", address );
			value = T( -1 );
			cycles = 1;
		}
		else
		{
			dbLogWarning( "Unhandled memory write [%X <= %X]", address, value );
		}
	}

	if constexpr ( ReadMode )
		m_eventManager.AddCycles( cycles );
}

// force template instantiations for unsigned read/write access

template
void MemoryMap::Access<uint8_t, true>( uint32_t address, uint8_t& value ) noexcept;

template
void MemoryMap::Access<uint8_t, false>( uint32_t address, uint8_t& value ) noexcept;

template
void MemoryMap::Access<uint16_t, true>( uint32_t address, uint16_t& value ) noexcept;

template
void MemoryMap::Access<uint16_t, false>( uint32_t address, uint16_t& value ) noexcept;

template
void MemoryMap::Access<uint32_t, true>( uint32_t address, uint32_t& value ) noexcept;

template
void MemoryMap::Access<uint32_t, false>( uint32_t address, uint32_t& value ) noexcept;

template <typename T, bool ReadMode>
STDX_forceinline void MemoryMap::AccessControllerPort( uint32_t offset, T& value ) noexcept
{
	if constexpr ( ReadMode )
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

template <typename T, bool ReadMode>
STDX_forceinline void MemoryMap::AccessSerialPort( uint32_t offset, T& value ) noexcept
{
	if constexpr ( ReadMode )
	{
		switch ( offset / 2 )
		{
			// 32bit registers
			case 0:
			case 1:	value = static_cast<T>( m_serialPort.ReadData() );					break;
			case 2:
			case 3:	value = static_cast<T>( m_serialPort.ReadStatus() );				break;

			// 16bit registers
			case 4:	value = static_cast<T>( m_serialPort.ReadMode() );					break;
			case 5:	value = static_cast<T>( m_serialPort.ReadControl() );				break;
			case 6:	value = static_cast<T>( m_serialPort.ReadMisc() );					break;
			case 7:	value = static_cast<T>( m_serialPort.ReadBaudrateReloadValue() );	break;

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
			case 1:	m_serialPort.WriteData( ShiftValueForWrite<uint32_t>( value, offset ) );				break;
			case 2:
			case 3:																							break; // status is read-only

			// 16bit registers
			case 4:	m_serialPort.WriteMode( ShiftValueForWrite<uint16_t>( value, offset ) );				break;
			case 5:	m_serialPort.WriteControl( ShiftValueForWrite<uint16_t>( value, offset ) );				break;
			case 6:	m_serialPort.WriteMisc( ShiftValueForWrite<uint16_t>( value, offset ) );				break;
			case 7:	m_serialPort.WriteBaudrateReloadValue( ShiftValueForWrite<uint16_t>( value, offset ) );	break;

			default:
				dbBreak();
				break;
		}
	}
}

template <typename T, bool ReadMode>
STDX_forceinline void MemoryMap::AccessSpu( uint32_t offset, T& value ) noexcept
{
	const auto shortOffset = offset / 2;

	if constexpr ( ReadMode )
	{
		if constexpr ( sizeof( T ) >= 2 )
			value = m_spu.Read( shortOffset );

		if constexpr ( sizeof( T ) == 4 )
			value |= static_cast<T>( m_spu.Read( shortOffset + 1 ) ) << 16;

		if constexpr ( sizeof( T ) == 1 )
			value = ShiftValueForRead<T>( m_spu.Read( shortOffset ), offset );
	}
	else
	{
		if constexpr ( sizeof( T ) >= 2 )
			m_spu.Write( shortOffset, static_cast<uint16_t>( value ) );

		if constexpr ( sizeof( T ) == 4 )
			m_spu.Write( shortOffset + 1, static_cast<uint16_t>( value >> 16 ) );

		if constexpr ( sizeof( T ) == 1 )
			m_spu.Write( shortOffset, ShiftValueForWrite<uint16_t>( value, offset ) );
	}
}

template <typename T, bool ReadMode>
STDX_forceinline void MemoryMap::AccessCDRomDrive( uint32_t offset, T& value ) noexcept
{
	if constexpr ( ReadMode )
	{
		value = m_cdRomDrive.Read( offset );

		if constexpr ( sizeof( T ) >= 2 )
			value |= ( static_cast<T>( m_cdRomDrive.Read( offset + 1 ) ) << 8 );

		if constexpr ( sizeof( T ) == 4 )
		{
			value |= ( static_cast<T>( m_cdRomDrive.Read( offset + 2 ) ) << 16 );
			value |= ( static_cast<T>( m_cdRomDrive.Read( offset + 3 ) ) << 24 );
		}
	}
	else
	{
		m_cdRomDrive.Write( offset, static_cast<uint8_t>( value ) );

		if constexpr ( sizeof( T ) >= 2 )
			m_cdRomDrive.Write( offset + 1, static_cast<uint8_t>( value >> 8 ) );

		if constexpr ( sizeof( T ) == 4 )
		{
			m_cdRomDrive.Write( offset + 2, static_cast<uint8_t>( value >> 16 ) );
			m_cdRomDrive.Write( offset + 3, static_cast<uint8_t>( value >> 24 ) );
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

void MemoryMap::Serialize( SaveStateSerializer& serializer )
{
	if ( !serializer.Header( "MemoryMap", 2 ) )
		return;

	for ( auto& cache : m_icacheFlags )
	{
		serializer( cache.value );
	}

	m_memoryControl.Serialize( serializer );
}

}