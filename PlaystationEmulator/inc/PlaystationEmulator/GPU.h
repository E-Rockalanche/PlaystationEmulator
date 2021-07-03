#pragma once

#include "FifoBuffer.h"

#include <stdx/assert.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace PSX
{

class CycleScheduler;
class Renderer;
class Timers;
class InterruptControl;

static constexpr float CpuClockSpeed = 44100 * 0x300; // Hz
static constexpr float VideoClockSpeed = CpuClockSpeed * 11 / 7; // Hz

static constexpr float RefreshRatePAL = 50;
static constexpr float RefreshRateNTSC = 60;

static constexpr uint32_t ScanlinesPAL = 314;
static constexpr uint32_t ScanlinesNTSC = 263;

constexpr float ConvertCpuToVideoCycles( float cycles ) noexcept
{
	return ( cycles * VideoClockSpeed ) / CpuClockSpeed;
}

constexpr float ConvertVideoToCpuCycles( float cycles ) noexcept
{
	return ( cycles * CpuClockSpeed ) / VideoClockSpeed;
}

class Gpu
{
public:

	static constexpr uint32_t VRamWidth = 1024;
	static constexpr uint32_t VRamHeight = 512;

	union Status
	{
		struct
		{
			// draw mode
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

			// 
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

		uint16_t GetCheckMask() const noexcept { return static_cast<uint16_t>( checkMaskOnDraw << 15 ); }
		uint16_t GetSetMask() const noexcept { return static_cast<uint16_t>( setMaskOnDraw << 15 ); }

		uint16_t GetTexPage() const noexcept
		{
			return static_cast<uint16_t>( ( value & 0x3ff ) | ( textureDisable << 11 ) );
		}
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

	Gpu( Timers& timers, InterruptControl& interruptControl, Renderer& renderer, CycleScheduler& cycleScheduler );

	void Reset();

	void WriteGP0( uint32_t value ) noexcept { std::invoke( m_gp0Mode, this, value ); }
	void WriteGP1( uint32_t value ) noexcept;

	uint32_t GpuRead() const noexcept { return m_gpuRead; }
	uint32_t GpuStatus() noexcept;

	bool IsInterlaced() const noexcept { return m_status.verticalResolution && m_status.verticalInterlace; }

	uint32_t GetHorizontalResolution() const noexcept;
	uint32_t GetVerticalResolution() const noexcept { return IsInterlaced() ? 480 : 240; }

	uint32_t GetScanlines() const noexcept { return m_status.videoMode ? ScanlinesPAL : ScanlinesNTSC; }
	float GetRefreshRate() const noexcept { return m_status.videoMode ? RefreshRatePAL : RefreshRateNTSC; }

	void UpdateTimers( uint32_t cpuTicks ) noexcept;
	uint32_t GetCpuCyclesUntilEvent() const noexcept;

	bool GetDisplayFrame() noexcept
	{
		return std::exchange( m_displayFrame, false );
	}

private:
	using GP0Function = void( Gpu::* )( uint32_t ) noexcept;

	using CommandFunction = void( Gpu::* )( ) noexcept;

	void ClearCommandBuffer()
	{
		m_commandBuffer.Reset();
		m_remainingParamaters = 0;
		m_commandFunction = nullptr;

		m_gp0Mode = &Gpu::GP0Command;
	}

	void InitCommand( uint32_t command, uint32_t paramaterCount, CommandFunction function )
	{
		dbExpects( m_commandBuffer.Empty() );
		m_commandBuffer.Push( command );
		m_remainingParamaters = paramaterCount;
		m_commandFunction = function;

		m_gp0Mode = &Gpu::GP0Params;
	}

	void SetDrawOffset( int16_t x, int16_t y );

	// GP0 modes
	void GP0Command( uint32_t ) noexcept;
	void GP0Params( uint32_t ) noexcept;
	void GP0PolyLine( uint32_t ) noexcept;
	void GP0ImageLoad( uint32_t ) noexcept; // affected by mask settings

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

	float GetCyclesPerFrame() const noexcept
	{
		return VideoClockSpeed / GetRefreshRate();
	}

	float GetCyclesPerScanline() const noexcept
	{
		return GetCyclesPerFrame() / GetScanlines();
	}

	float GetDotsPerCycle() const noexcept
	{
		return GetHorizontalResolution() / 2560.0f;
	}

	float GetDotsPerScanline() const noexcept
	{
		return GetDotsPerCycle() * GetCyclesPerScanline();
	}

	// operations on vram

	// not affected by mask settings
	void FillVRam( uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint16_t value );

	// affected by mask settings
	void CopyVRam( uint32_t srcX, uint32_t srcY, uint32_t destX, uint32_t destY, uint32_t width, uint32_t height );

	void FlushVRam();

private:
	Timers& m_timers;
	InterruptControl& m_interruptControl;
	Renderer& m_renderer;
	CycleScheduler& m_cycleScheduler;

	FifoBuffer<uint32_t, 16> m_commandBuffer;
	uint32_t m_remainingParamaters;
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

	// use SetDrawOffset(x,y) to change values
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
	float m_currentDot;
	float m_dotTimerFraction;
	bool m_hblank;
	bool m_vblank;

	bool m_displayFrame;

	uint32_t m_totalCpuCyclesThisFrame = 0; // temp

	std::unique_ptr<uint16_t[]> m_vram; // 1MB of VRAM, 1024x512
	bool m_vramDirty;

	struct VRamCopyState
	{
		uint32_t left = 0;
		uint32_t top = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t x = 0;
		uint32_t y = 0;

		bool Finished() const noexcept { return x == 0 && y == height; }

		uint32_t GetWrappedX() const noexcept { return ( left + x ) % VRamWidth; }

		uint32_t GetWrappedY() const noexcept { return ( top + y ) % VRamHeight; }

		void Increment() noexcept
		{
			if ( ++x == width )
			{
				x = 0;
				++y;
			}
		}
	};

	std::optional<VRamCopyState> m_vramCopyState;

};

}