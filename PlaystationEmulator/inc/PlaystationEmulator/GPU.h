#pragma once

#include "assert.h"
#include "Memory.h"

#include <cstdint>

namespace PSX
{


class Gpu
{
public:

	union Status
	{
		struct
		{
			uint32_t texturePageBaseX : 4;
			uint32_t texturePageBaseY : 1;
			uint32_t semiTransparency : 2;
			uint32_t texturePageColors : 2;
			uint32_t dither : 1;
			uint32_t drawToDisplayArea : 1;
			uint32_t setMaskOnDraw : 1;
			uint32_t checkMaskOnDraw : 1;
			uint32_t interlaceField : 1;
			uint32_t reverseFlag : 1;
			uint32_t textureDisable : 1;
			uint32_t horizontalResolution2 : 1;
			uint32_t horizontalResolution1 : 2;
			uint32_t verticalResolution : 1;
			uint32_t videoMode : 1;
			uint32_t displayAreaColorDepth : 1;
			uint32_t verticalInterlace : 1;
			uint32_t displayDisable : 1;
			uint32_t interruptRequest : 1;
			uint32_t dmaRequest : 1;
			uint32_t readyToReceiveCommand : 1;
			uint32_t readyToSendVRamToCpu : 1;
			uint32_t readyToReceiveDmaBlock : 1;
			uint32_t dmaDirection : 2;
			uint32_t drawingEvenOdd : 1;
		};

		uint32_t value;
	};
	static_assert( sizeof( Status ) == 4 );

	enum class SemiTransparency : uint8_t
	{
		Blend,
		Add,
		Sub,
		AddQuarter
	};

	enum class TexturePageColors : uint8_t
	{
		B4,
		B8,
		B15
	};

	enum class DrawPixelMode : uint8_t
	{
		Always,
		NotToMaskedAreas
	};

	enum class InterlaceField : uint8_t
	{
		Top,
		Bottom
	};

	enum class HorizontalResolution : uint8_t
	{
		P256 = 0,
		P368 = 1, // always if bit 0 is set
		P320 = 2,
		P512 = 4,
		P640 = 6
	};

	enum class VerticalResolution : uint8_t
	{
		P240,
		P480
	};

	struct VideoMode
	{
		enum : uint8_t
		{
			NTSC, // 60Hz
			PAL // 50Hz
		};
	};

	enum class DisplayAreaColorDepth : uint8_t
	{
		B15,
		B24
	};

	struct DmaDirection
	{
		enum : uint8_t
		{
			Off,
			Fifo,
			CpuToGP0,
			GpuReadToCpu
		};
	};

	void Reset();

	void WriteGP0( uint32_t value ) noexcept;
	void WriteGP1( uint32_t value ) noexcept;

	uint32_t GpuRead() const noexcept
	{
		return m_gpuRead;
	}

	uint32_t GpuStatus() const noexcept
	{
		return m_status.value;
	}

	uint32_t GetHorizontalResolution() const noexcept;

	uint32_t GetVerticalResolution() const noexcept
	{
		return ( m_status.verticalResolution && m_status.verticalInterlace ) ? 480 : 240;
	}

private:
	uint32_t m_gpuRead;

	Status m_status;

	// draw mode
	bool m_texturedRectFlipX;
	bool m_texturedRectFlipY;

	// texture window
	uint8_t m_textureWindowMaskX;
	uint8_t m_textureWindowMaskY;
	uint8_t m_textureWindowOffsetX;
	uint8_t m_textureWindowOffsetY;

	// drawing area
	uint16_t m_drawAreaLeft;
	uint16_t m_drawAreaTop;
	uint16_t m_drawAreaRight;
	uint16_t m_drawAreaBottom;

	// drawing offset
	int16_t m_drawOffsetX;
	int16_t m_drawOffsetY;

	// start of display area
	uint16_t m_displayAreaStartX;
	uint16_t m_displayAreaStartY;

	// horizontal display range
	uint16_t m_horDisplayRange1;
	uint16_t m_horDisplayRange2;

	// vertical display range
	uint16_t m_verDisplayRange1;
	uint16_t m_verDisplayRange2;

	Memory<1024 * 1024> m_vram;
};

}