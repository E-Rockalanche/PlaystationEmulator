#pragma once

#include "Defs.h"

#include <stdx/assert.h>

#include <array>
#include <cstdint>

namespace PSX
{

class SaveStateSerializer;

class MemoryControl
{
public:
	enum class DelaySizeType
	{
		Expansion1,
		Expansion3,
		Bios,
		Spu,
		CDRom,
		Expansion2
	};

	union DelaySizeRegister
	{
		static constexpr uint32_t WriteMask = 0xcf1fffff;

		struct
		{
			uint32_t : 4;					// R/W
			uint32_t accessTime : 4;		// 00h..0Fh=00h..0Fh Cycles
			uint32_t useCom0Time : 1;		// 0=No, 1=Yes, add to Access Time
			uint32_t useCom1Time : 1;		// 0=No, 1=Probably Yes, but has no effect?
			uint32_t useCom2Time : 1;		// 0=No, 1=Yes, add to Access Time
			uint32_t useCom3Time : 1;		// 0=No, 1=Yes, clip to MIN=(COM3+6) or so?
			uint32_t dataBusWidth : 1;		// 0=8bit, 1=16bit
			uint32_t : 3;					// R/W
			uint32_t memoryWindowSize : 5;	// (1 SHL N bytes) (0..1Fh = 1 byte ... 2 gigabytes)
			uint32_t : 11;					// bits 24-27, 29, 31 R/W
		};
		uint32_t value = 0;
	};
	static_assert( sizeof( DelaySizeRegister ) == 4 );

	struct DelaySize
	{
		DelaySizeRegister reg;
		std::array<cycles_t, 3> accessTimes{}; // 8bit, 16bit, 32bit
	};

public:
	void Reset();

	uint32_t Read( uint32_t index ) const noexcept;

	void Write( uint32_t index, uint32_t value ) noexcept;

	uint32_t ReadRamSize() const noexcept
	{
		return m_ramSize.value;
	}

	void WriteRamSize( uint32_t value ) noexcept
	{
		m_ramSize.value = value;
	}

	bool MirrorRam( uint32_t ksegment ) const noexcept { return m_ramSize.value & ( 1 << ( 9 + ksegment ) ); }

	uint32_t ReadCacheControl() const noexcept
	{
		return m_cacheControl;
	}

	void WriteCacheControl( uint32_t value ) noexcept
	{
		m_cacheControl = value & CacheControl::WriteMask;
	}

	void Serialize( SaveStateSerializer& serializer );

	template <typename T>
	cycles_t GetAccessCycles( DelaySizeType type ) const noexcept
	{
		static constexpr size_t cyclesIndex = sizeof( T ) <= 2 ? sizeof( T ) - 1 : 2;
		return m_delaySizes[ static_cast<size_t>( type ) ].accessTimes[ cyclesIndex ];
	}

private:

	union ComDelay
	{
		static constexpr uint32_t WriteMask = 0x000fffff;

		struct
		{
			uint32_t com0 : 4;	// Offset A   ;used for SPU/EXP2 (and for adjusted CDROM timings)
			uint32_t com1 : 4;	// No effect? ;used for EXP2
			uint32_t com2 : 4;	// Offset B   ;used for BIOS/EXP1/EXP2
			uint32_t com3 : 4;	// Min Value  ;used for CDROM
			uint32_t com4 : 4;	// Unknown    ;used for whatever
			uint32_t : 12;		// Unknown/unused (read: always 0000h)
		};
		uint32_t value = 0;
	};
	static_assert( sizeof( ComDelay ) == 4 );

	union RamSizeRegister
	{
		struct
		{
			uint32_t : 7;

			uint32_t delaySimultaneousCodeDataFetch : 1; // 0=None, 1=One Cycle

			uint32_t : 1;

			uint32_t memoryWindow : 3; // first 8MB of KUSEG,KSEG0,KSEG1
			/*
			Possible values for Bit9 - 11 are:
			0 = 1MB Memory + 7MB Locked
				1 = 4MB Memory + 4MB Locked
				2 = 1MB Memory + 1MB HighZ + 6MB Locked
				3 = 4MB Memory + 4MB HighZ
				4 = 2MB Memory + 6MB Locked; < -- - would be correct for PSX
				5 = 8MB Memory; < -- - default by BIOS init
				6 = 2MB Memory + 2MB HighZ + 4MB Locked; < --HighZ = Second / RAS
				7 = 8MB Memory
			*/

			uint32_t : 20;
		};
		uint32_t value = 0;
	};
	static_assert( sizeof( RamSizeRegister ) == 4 );

	struct CacheControl
	{
		enum : uint32_t
		{
			ScratchpadEnable = ( 1 << 3 ) | ( 1 << 7 ),
			CodeCacheEnable = 1 << 11,

			WriteMask = 0xfffffddf,
		};
	};

private:
	void CalculateAccessTime( DelaySize& delaySize ) noexcept;

private:
	uint32_t m_expension1BaseAddress = 0;
	uint32_t m_expension2BaseAddress = 0;

	std::array<DelaySize, 6> m_delaySizes;

	ComDelay m_comDelay;

	RamSizeRegister m_ramSize;

	uint32_t m_cacheControl = 0;
};

}