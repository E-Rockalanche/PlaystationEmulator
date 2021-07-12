#pragma once

// https://problemkaputt.de/psx-spx.htm#memorycontrol

#include <stdx/assert.h>

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

public:
	MemoryControl()
	{
		Reset();
	}

	void Reset()
	{
		m_registers.fill( 0 );
		m_ramSize = 0;
		m_cacheControl = 0;
	}

	uint32_t Read( uint32_t index ) const noexcept
	{
		return m_registers[ index ];
	}

	void Write( uint32_t index, uint32_t value ) noexcept;

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
		return m_cacheControl;
	}

	void WriteCacheControl( uint32_t value ) noexcept
	{
		m_cacheControl = value & 0xfffffddf;
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