#pragma once

#include "FifoBuffer.h"
#include "Timers.h"

#include "assert.h"

#include <cstdint>

namespace PSX
{

class Renderer;

class Gpu
{
public:
	static constexpr uint32_t CpuClockSpeed = 44100 * 0x300; // Hz
	static constexpr uint32_t VideoClockSpeed = 44100 * 0x300 * 11 / 7; // Hz
	static constexpr uint32_t RefreshRatePAL = 50;
	static constexpr uint32_t RefreshRateNTSC = 60;
	static constexpr uint32_t ScanlinesPAL = 314;
	static constexpr uint32_t ScanlinesNTSC = 263;

	union Status
	{
		struct
		{
			uint32_t texturePageBaseX : 4; // N*64
			uint32_t texturePageBaseY : 1; // N*256
			uint32_t semiTransparency : 2; // 0=B/2+F/2, 1=B+F, 2=B-F, 3=B+F/4
			uint32_t texturePageColors : 2; // 0=4bit, 1=8bit, 2=15bit
			uint32_t dither : 1; // 0=Off/strip LSBs, 1=Dither Enabled
			uint32_t drawToDisplayArea : 1;
			uint32_t setMaskOnDraw : 1;
			uint32_t checkMaskOnDraw : 1;
			uint32_t interlaceField : 1;
			uint32_t reverseFlag : 1;
			uint32_t textureDisable : 1;
			uint32_t horizontalResolution2 : 1; // 0=256/320/512/640, 1=368
			uint32_t horizontalResolution1 : 2; // 0=256, 1=320, 2=512, 3=640
			uint32_t verticalResolution : 1; // 0=240, 1=480, when VerticalInterlace=1
			uint32_t videoMode : 1; // 0=NTSC/60Hz, 1=PAL/50Hz
			uint32_t displayAreaColorDepth : 1; // 0=15bit, 1=24bit
			uint32_t verticalInterlace : 1;
			uint32_t displayDisable : 1;
			uint32_t interruptRequest : 1;
			uint32_t dmaRequest : 1;
			uint32_t readyToReceiveCommand : 1;
			uint32_t readyToSendVRamToCpu : 1;
			uint32_t readyToReceiveDmaBlock : 1;
			uint32_t dmaDirection : 2; // 0=Off, 1=?, 2=CPUtoGP0, 3=GPUREADtoCPU
			uint32_t drawingEvenOdd : 1; // 0=Even or Vblank, 1=Odd
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

	enum class VideoMode
	{
		NTSC,
		PAL
	};

	enum class VerticalResolution : uint8_t
	{
		P240,
		P480
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

	Gpu( Timers& timers, Renderer& renderer ) : m_timers{ timers }, m_renderer { renderer } {}

	void Reset();

	void WriteGP0( uint32_t value ) noexcept { std::invoke( m_gp0Mode, this, value ); }

	void WriteGP1( uint32_t value ) noexcept;

	uint32_t GpuRead() const noexcept { return m_gpuRead; }

	uint32_t GpuStatus() const noexcept
	{
		return m_status.value;

			// return vertical resolution as 240 so that the BIOS doesn't infinite loop waiting for even/odd line flag to change
			// TODO: revert when status bit 31 is implemented
			// & ~( 1u << 19 );
	}

	bool IsInterlaced() const noexcept { return m_status.verticalResolution && m_status.verticalInterlace; }

	uint32_t GetHorizontalResolution() const noexcept;
	uint32_t GetVerticalResolution() const noexcept { return IsInterlaced() ? 480 : 240; }

	uint32_t GetScanlines() const noexcept { return m_status.videoMode ? ScanlinesPAL : ScanlinesNTSC; }
	uint32_t GetRefreshRate() const noexcept { return m_status.videoMode ? RefreshRatePAL : RefreshRateNTSC; }

	static uint32_t ConvertCpuToVideoCycles( uint32_t cycles ) noexcept
	{
		return ( cycles * VideoClockSpeed ) / CpuClockSpeed;
	}
	static uint32_t ConvertVideoToCpuCycles( uint32_t cycles ) noexcept
	{
		return ( cycles * CpuClockSpeed ) / VideoClockSpeed;
	}

private:
	using GP0Function = void( Gpu::* )( uint32_t ) noexcept;

	using CommandFunction = void( Gpu::* )( ) noexcept;

	void UpdateTimerState() const;

	void ClearCommandBuffer()
	{
		m_commandBuffer.Reset();
		m_remainingWords = 0;
		m_commandFunction = nullptr;

		m_gp0Mode = &Gpu::GP0Command;
	}

	void InitCommand( uint32_t command, uint32_t remainingWords, CommandFunction function )
	{
		dbExpects( m_commandBuffer.Empty() );
		m_commandBuffer.Push( command );
		m_remainingWords = remainingWords;
		m_commandFunction = function;

		m_gp0Mode = &Gpu::GP0Params;
	}

	void SetDrawOffset( int16_t x, int16_t y ) noexcept;

	// GP0 modes
	void GP0Command( uint32_t ) noexcept;
	void GP0Params( uint32_t ) noexcept;
	void GP0PolyLine( uint32_t ) noexcept;
	void GP0ImageLoad( uint32_t ) noexcept;

	// command functions
	void FillRectangle() noexcept;
	void CopyRectangle() noexcept;
	void CopyRectangleToVram() noexcept;
	void CopyRectangleFromVram() noexcept;

	// render commands
	void RenderPolygon() noexcept;
	void RenderRectangle() noexcept;

	void TempFinishCommandParams() noexcept
	{
		ClearCommandBuffer();
	}

private:
	Timers& m_timers;
	Renderer& m_renderer;

	FifoBuffer<uint32_t, 16> m_commandBuffer;
	uint32_t m_remainingWords;
	CommandFunction m_commandFunction;

	GP0Function m_gp0Mode;

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

	// timing
	uint32_t m_currentScanline;
	uint32_t m_currentDot;
	bool m_hblank;
	bool m_vblank;
	bool m_oddFrame;
};

}