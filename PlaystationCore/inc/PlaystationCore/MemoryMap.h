#pragma once

#include "Defs.h"
#include "MemoryControl.h"

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
		ControllerSize = 0x10,

		SerialPortStart = 0x1f801050,
		SerialPortSize = 0x10,

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

		Expansion3Start = 0x1fa00000,
		Expansion3Size = 2 * 1024 * 1024, // default 1 byte, max 2 MBytes

		BiosStart = 0x1fc00000,
		BiosSize = 512 * 1024,

		CacheControlStart = 0xfffe0130,
		CacheControlSize = 4
	};

public:
	MemoryMap(
		EventManager& eventManager,
		Bios& bios,
		CDRomDrive& cdRomDrive,
		ControllerPorts& controllerPorts,
		Dma& dma,
		Gpu& gpu,
		InterruptControl& interruptControl,
		MacroblockDecoder& mdec,
		Ram& ram,
		Scratchpad& scratchpad,
		SerialPort& serialPort,
		Spu& spu,
		Timers& timers )
		: m_eventManager{ eventManager }
		, m_bios{ bios }
		, m_cdRomDrive{ cdRomDrive }
		, m_controllerPorts{ controllerPorts }
		, m_dma{ dma }
		, m_gpu{ gpu }
		, m_interruptControl{ interruptControl }
		, m_mdec{ mdec }
		, m_ram{ ram }
		, m_scratchpad{ scratchpad }
		, m_serialPort{ serialPort }
		, m_spu{ spu }
		, m_timers{ timers }
	{}

	void Reset();

	template <typename T>
	T Read( uint32_t address ) noexcept
	{
		using UT = std::make_unsigned_t<T>;
		UT value;
		Access<UT, true>( address, value );
		return static_cast<T>( value );
	}

	template <typename T>
	void Write( uint32_t address, T value ) noexcept
	{
		using UT = std::make_unsigned_t<T>;
		UT uvalue = static_cast<UT>( value );
		Access<UT, false>( address, uvalue );
	}

	void SetDualSerialPort( DualSerialPort* dualSerialPort ) noexcept
	{
		m_dualSerialPort = dualSerialPort;
	}

	std::optional<Instruction> FetchInstruction( uint32_t address ) noexcept;

	// doesn't add cycles or fill cache
	std::optional<Instruction> PeekInstruction( uint32_t address ) noexcept;

	void WriteICache( uint32_t address, uint32_t ) noexcept
	{
		dbExpects( address / 16 < m_icacheFlags.size() );
		m_icacheFlags[ address / 16 ].valid = 0;
		// TODO: write to icache
	}

	// convert PSX address to physical address
	const uint8_t* GetRealAddress( uint32_t address ) const noexcept;

	Ram& GetRam() noexcept { return m_ram; }

	void Serialize( SaveStateSerializer& serializer );

private:
	static constexpr cycles_t RamReadCycles = 4;
	static constexpr cycles_t DeviceReadCycles = 2;

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

	union ICacheFlags
	{
		struct
		{
			uint32_t tag : 20;
			uint32_t valid : 4;
			uint32_t : 8;
		};
		uint32_t value = 0;
	};
	static_assert( sizeof( ICacheFlags ) == 4 );

private:
	template <typename T, bool Read>
	void Access( uint32_t address, T& value ) noexcept;

	// returns byte shift amount for unaligned register address
	template <typename RegType>
	STDX_forceinline static constexpr uint32_t GetShift( uint32_t address ) noexcept
	{
		return ( address % sizeof( RegType ) ) * 8;
	}

	// return shifted value for unaligned register write
	template <typename RegType, typename T>
	STDX_forceinline static constexpr RegType ShiftValueForWrite( T value, uint32_t address ) noexcept
	{
		return static_cast<RegType>( value ) << GetShift<RegType>( address );
	}

	// return shifted value for unaligned register read
	template <typename T, typename RegType>
	STDX_forceinline static constexpr T ShiftValueForRead( RegType value, uint32_t address ) noexcept
	{
		return static_cast<T>( value >> GetShift<RegType>( address ) );
	}

	template <typename T, bool Read, typename MemoryType>
	STDX_forceinline static void AccessMemory( MemoryType& memory, uint32_t offset, T& value ) noexcept
	{
		if constexpr ( Read )
			value = memory.template Read<T>( offset );
		else
			memory.template Write<T>( offset, value );
	}

	template <typename T, bool Read, typename Component>
	STDX_forceinline static void AccessComponent32( Component& component, uint32_t offset, T& value ) noexcept
	{
		if constexpr ( Read )
			value = ShiftValueForRead<T>( component.Read( offset / 4 ), offset );
		else
			component.Write( offset / 4, ShiftValueForWrite<uint32_t>( value, offset ) );
	}

	template <typename T, bool Read>
	void AccessControllerPort( uint32_t offset, T& value ) noexcept;

	template <typename T, bool Read>
	void AccessSerialPort( uint32_t offset, T& value ) noexcept;

	template <typename T, bool Read>
	void AccessSpu( uint32_t offset, T& value ) noexcept;

	template <typename T, bool Read>
	void AccessCDRomDrive( uint32_t offset, T& value ) noexcept;

	// returns the number of instructions fetched
	uint32_t CheckAndPrefetchICache( uint32_t address ) noexcept;

	template <bool AddCycles, bool PreFetchRead, uint32_t FetchCount>
	std::optional<Instruction> ReadInstruction( uint32_t address ) noexcept;

private:
	EventManager& m_eventManager;
	Bios& m_bios;
	CDRomDrive& m_cdRomDrive;
	ControllerPorts& m_controllerPorts;
	Dma& m_dma;
	Gpu& m_gpu;
	InterruptControl& m_interruptControl;
	MacroblockDecoder& m_mdec;
	Ram& m_ram;
	Scratchpad& m_scratchpad;
	SerialPort& m_serialPort;
	Spu& m_spu;
	Timers& m_timers;

	DualSerialPort* m_dualSerialPort = nullptr;

	MemoryControl m_memoryControl;

	std::array<ICacheFlags, 256> m_icacheFlags;
};

}