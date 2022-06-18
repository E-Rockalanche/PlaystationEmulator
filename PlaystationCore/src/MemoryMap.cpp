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

template <typename T, bool Read>
void MemoryMap::Access( uint32_t address, T& value ) noexcept
{
	static_assert( std::is_unsigned_v<T> ); // don't want to duplicate Access function for signed and unsigned types

	cycles_t cycles = Read ? DeviceReadCycles : 0;

	// upper 3 bits determine segment
	// convert virtual address to physical address
	address &= RegionMasks[ address >> 29 ];

	if ( address <= RamMirrorSize ) // ram starts at 0
	{
		AccessMemory<T, Read>( m_ram, address % RamSize, value );
		if constexpr ( Read )
			cycles = RamReadCycles;
	}
	else if ( Within( address, BiosStart, BiosSize ) )
	{
		// read only
		if constexpr ( Read )
		{
			value = m_bios.Read<T>( address - BiosStart );
			cycles = m_memoryControl.GetAccessCycles<T>( MemoryControl::DelaySizeType::Bios );
		}
	}
	else if ( Within( address, ScratchpadStart, ScratchpadSize ) )
	{
		AccessMemory<T, Read>( m_scratchpad, address - ScratchpadStart, value );
		return; // 0 cycles
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
		AccessSerialPort<T, Read>( address - SerialPortStart, value );
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
		AccessCDRomDrive<T, Read>( address - CdRomStart, value );

		if constexpr ( Read )
			cycles = m_memoryControl.GetAccessCycles<T>( MemoryControl::DelaySizeType::CDRom );
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
		if constexpr ( Read )
			cycles = m_memoryControl.GetAccessCycles<T>( MemoryControl::DelaySizeType::Spu );
	}
	else if ( Within( address, CacheControlStart, CacheControlSize ) )
	{
		if constexpr ( Read )
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
		if constexpr ( Read )
		{
			value = T( -1 );
			cycles = m_memoryControl.GetAccessCycles<T>( MemoryControl::DelaySizeType::Expansion1 );
		}
	}
	else if ( Within( address, Expansion2Start, Expansion2Size ) )
	{
		if constexpr ( Read )
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
		if constexpr ( Read )
		{
			value = T( -1 );
			cycles = m_memoryControl.GetAccessCycles<T>( MemoryControl::DelaySizeType::Expansion3 );
		}
	}
	else
	{
		if constexpr ( Read )
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

	if constexpr ( Read )
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

template <typename T, bool Read>
STDX_forceinline void MemoryMap::AccessControllerPort( uint32_t offset, T& value ) noexcept
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
STDX_forceinline void MemoryMap::AccessSerialPort( uint32_t offset, T& value ) noexcept
{
	if constexpr ( Read )
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

template <typename T, bool Read>
STDX_forceinline void MemoryMap::AccessSpu( uint32_t offset, T& value ) noexcept
{
	const auto shortOffset = offset / 2;

	if constexpr ( Read )
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

template <typename T, bool Read>
STDX_forceinline void MemoryMap::AccessCDRomDrive( uint32_t offset, T& value ) noexcept
{
	if constexpr ( Read )
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

uint32_t MemoryMap::CheckAndPrefetchICache( uint32_t address ) noexcept
{
	dbExpects( address % 4 == 0 ); // instructions must be word-aligned

	uint32_t fetchCount = 0;

	const uint32_t index = ( address >> 2 ) & 0x3u;
	const uint32_t line = ( address >> 4 ) & 0xffu;
	const uint32_t tag = ( address >> 12 );

	auto& flags = m_icacheFlags[ line ];
	const bool cached = ( flags.tag == tag ) && ( flags.valid & ( 1u << index ) );
	if ( !cached )
	{
		flags.tag = tag;
		flags.valid = ( 0xfu << index ) & 0xfu;
		fetchCount = 4 - index;

		// TODO: pre-fetch next instructions in 4-word block
	}

	return fetchCount;
}

std::optional<Instruction> MemoryMap::FetchInstruction( uint32_t address ) noexcept
{
	dbExpects( address % 4 == 0 );

	const uint32_t segment = address >> 29;
	switch ( segment )
	{
		case 0:
		case 4:
			// read cached
			switch ( CheckAndPrefetchICache( address ) )
			{
				case 0:
					// read from icache takes no cycles
					return ReadInstruction<false, false, 0>( address );

				case 1:
					return ReadInstruction<true, true, 1>( address );

				case 2:
					return ReadInstruction<true, true, 2>( address );

				case 3:
					return ReadInstruction<true, true, 3>( address );

				default:
					dbBreak();
					[[fallthrough]];
				case 4:
					return ReadInstruction<true, true, 4>( address );
			}
			break;

		case 5:
			// read uncached
			return ReadInstruction<true, false, 1>( address );

		default:
			// raise exception
			return std::nullopt;
	}
}

std::optional<Instruction> MemoryMap::PeekInstruction( uint32_t address ) noexcept
{
	dbExpects( address % 4 == 0 );

	const uint32_t segment = address >> 29;
	switch ( segment )
	{
		case 0:
		case 4:
		case 5:
			return ReadInstruction<false, false, 0>( address );

		default:
			return std::nullopt;
	}
}

template <bool AddCycles, bool PreFetchRead, uint32_t FetchCount>
STDX_forceinline std::optional<Instruction> MemoryMap::ReadInstruction( uint32_t address ) noexcept
{
	address &= RegionMasks[ address >> 29 ];

	if ( address < RamMirrorSize )
	{
		if constexpr ( AddCycles )
			m_eventManager.AddCycles( ( PreFetchRead ? 1 : RamReadCycles ) * FetchCount );

		return Instruction( m_ram.Read<uint32_t>( address % RamSize ) );
	}
	else if ( Within( address, BiosStart, BiosSize ) )
	{
		if constexpr ( AddCycles )
			m_eventManager.AddCycles( m_memoryControl.GetAccessCycles<uint32_t>( MemoryControl::DelaySizeType::Bios ) );

		return Instruction( m_bios.Read<uint32_t>( address - BiosStart ) * FetchCount );
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