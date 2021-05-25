#pragma once

#include "assert.h"
#include "Memory.h"

#include <cstdint>

namespace PSX
{

class Gpu
{
public:
	enum WritePort : uint32_t
	{
		GP0,
		GP1
	};

	enum ReadPort : uint32_t
	{
		GpuRead,
		GpuStatus
	};

	struct Status
	{
		enum : uint32_t
		{
			TexturePageXBase = 0xfu,
			TexturePageYBase = 1u << 4,
			SemiTranspareny = 0x3u << 5,
			TexturePageColors = 0x3u << 7,
			Dither24bitTo15bit = 1u << 9,
			DrawingToDisplayArea = 1u << 10,
			SetMaskBitWhenDrawingPixels = 1u << 11,
			DrawPixels = 1u << 12,
			InterlaceField = 1u << 13,
			ReverseFlag = 1u << 14,
			TextureDisable = 1u << 15,
			HorizontalResolution2 = 1u << 16,
			HorizontalResolution1 = 0x3u << 17,
			VerticalResolution = 1u << 19,
			VideoMode = 1u << 20,
			DisplayAreaColorDepth = 1u << 21,
			VerticalInterlace = 1u << 22,
			DisplayEnable = 1u << 23,
			InterruptRequest = 1u << 24,
			DmaDataRequest = 1u << 25,
			ReadyToReceiveCommandWord = 1u << 26,
			ReadyToSendVramToCpu = 1u << 27,
			ReadyToReceiveDmaBlock = 1u << 28,
			DmaDirection = 0x3u << 29,
			DrawingEvenOddLines = 1u << 31
		};
	};

	void Reset()
	{
		m_vram.Fill( char( -1 ) );
	}

	void Write( uint32_t index, uint32_t value ) noexcept // send gpu commands
	{
		if ( index == WritePort::GP0 )
		{
			dbLog( "write to GP0 [%X]", value );
		}
		else
		{
			dbLog( "write to GP1 [%X]", value );
		}
	}

	uint32_t Read( uint32_t index ) const noexcept // receive command responses
	{
		dbExpects( index < 2 );
		if ( index == ReadPort::GpuRead )
		{
			dbLog( "read from GPUREAD" );
			// TODO
			return 0;
		}
		else
		{
			dbLog( "read from GPUSTAT" );
			// TODO
			return Status::ReadyToReceiveCommandWord | Status::ReadyToSendVramToCpu | Status::ReadyToReceiveDmaBlock;
		}
	}

private:
	Memory<1024 * 1024> m_vram;
};

}