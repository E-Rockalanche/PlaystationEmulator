#pragma once

// https://problemkaputt.de/psx-spx.htm#memorycontrol

#include "assert.h"

#include <array>
#include <cstdint>

namespace PSX
{

class MemoryControl
{
public:
	enum Register : uint32_t
	{
		Expansion1BaseAddress,
		Expansion2BaseAddress,
		Expansion1DelaySize,
		Expansion3DelaySize,
		BiosRomDelaySize,
		SpuDelaySize,
		CDRomDelaySize,
		Expansion2DelaySize,
		CommonDelay,
		
		Count
	};

	enum CacheControl
	{
		ScratchpadEnable = 0x00000084,
		Crash = 0x00000200,
		CodeCacheEnable = 0x00000800,
	};

	uint32_t Read( uint32_t index ) const noexcept
	{
		return m_registers[ index ];
	}

	void Write( uint32_t index, uint32_t value ) noexcept
	{
		switch ( index )
		{
			case Register::Expansion1BaseAddress:
			case Register::Expansion2BaseAddress:
				m_registers[ index ] = 0x1f000000 | ( value & 0x00ffffff );
				break;

			case Register::Expansion1DelaySize:
			case Register::Expansion3DelaySize:
			case Register::BiosRomDelaySize:
			case Register::SpuDelaySize:
			case Register::CDRomDelaySize:
			case Register::Expansion2DelaySize:
				m_registers[ index ] = value & 0xabffffff;
				break;

			case Register::CommonDelay:
				m_registers[ Register::CommonDelay ] = value;
				break;

			default:
				dbBreak();
				break;
		}
	}

	uint32_t ReadRamSize() const noexcept
	{
		return m_ramSize;
	}

	void WriteRamSize( uint32_t value ) noexcept
	{
		m_ramSize = value;
	}

	bool MirrorRam( uint32_t ksegment ) const noexcept { return m_ramSize & ( 1 << ( 9 + ksegment ) ); }

	uint32_t ReadCacheControl() const noexcept
	{
		return m_cacheControl & 0xfffffddf;
	}

	void WriteCacheControl( uint32_t value ) noexcept
	{
		m_cacheControl = value;
	}

private:
	// 0x1f801000 - 0x1f801023
	std::array<uint32_t, Register::Count > m_registers;

	// 0x1f801060
	uint32_t m_ramSize = 0;

	// 0xfffe0130
	uint32_t m_cacheControl = 0;
};

}