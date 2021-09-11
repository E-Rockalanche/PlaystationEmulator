#pragma once

#include "FifoBuffer.h"
#include "GpuDefs.h"

#include <Math/Rectangle.h>

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

	union Status
	{
		Status() : value{ 0 } {}

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

		uint16_t GetCheckMask() const noexcept { return static_cast<uint16_t>( checkMaskOnDraw << 15 ); }
		uint16_t GetSetMask() const noexcept { return static_cast<uint16_t>( setMaskOnDraw << 15 ); }

		uint16_t GetDrawMode() const noexcept // used for textured primitives
		{
			return static_cast<uint16_t>( ( value & 0x3ff ) | ( textureDisable << 11 ) );
		}

		SemiTransparency GetSemiTransparency() const noexcept { return static_cast<SemiTransparency>( semiTransparency ); }
	};
	static_assert( sizeof( Status ) == 4 );

	Gpu( Timers& timers, InterruptControl& interruptControl, Renderer& renderer, CycleScheduler& cycleScheduler );

	void Reset();

	uint32_t Read( uint32_t index ) noexcept
	{
		dbExpects( index < 2 );
		if ( index == 0 )
			return GpuRead();
		else
			return GpuStatus();
	}

	void Write( uint32_t index, uint32_t value ) noexcept
	{
		dbExpects( index < 2 );
		if ( index == 0 )
			WriteGP0( value );
		else
			WriteGP1( value );
	}

	void WriteGP0( uint32_t value ) noexcept { std::invoke( m_gp0Mode, this, value ); }
	uint32_t GpuRead() noexcept { return std::invoke( m_gpuReadMode, this ); }

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
	using GpuReadFunction = uint32_t( Gpu::* )( ) noexcept;
	using CommandFunction = void( Gpu::* )( ) noexcept;

	void WriteGP1( uint32_t value ) noexcept;

	uint32_t GpuStatus() noexcept;

	void ClearCommandBuffer() noexcept;

	void InitCommand( uint32_t command, uint32_t paramaterCount, CommandFunction function ) noexcept;

	void SetupVRamCopy() noexcept;
	void FinishVRamTransfer() noexcept;

	void SetDrawOffset( int16_t x, int16_t y ) noexcept;

	// GP0 modes
	void GP0_Command( uint32_t ) noexcept;
	void GP0_Params( uint32_t ) noexcept;
	void GP0_PolyLine( uint32_t ) noexcept;
	void GP0_Image( uint32_t ) noexcept; // affected by mask settings

	void SetGP0Mode( GP0Function f ) noexcept
	{
		m_gp0Mode = f;

		// this line breaks amidog CPU tests for some reason
		// m_status.readyToReceiveCommand = ( f == &Gpu::GP0_Command );
	}

	// GPUREAD modes
	uint32_t GpuRead_Normal() noexcept;
	uint32_t GpuRead_Image() noexcept;

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

	float GetVideoCyclesPerFrame() const noexcept
	{
		return VideoClockSpeed / GetRefreshRate();
	}

	float GetVideoCyclesPerScanline() const noexcept
	{
		return GetVideoCyclesPerFrame() / GetScanlines();
	}

	float GetDotsPerVideoCycle() const noexcept
	{
		return GetHorizontalResolution() / 2560.0f;
	}

	float GetDotsPerScanline() const noexcept
	{
		return GetDotsPerVideoCycle() * GetVideoCyclesPerScanline();
	}

	// operations on vram

	// not affected by mask settings
	void FillVRam( uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint16_t value );

	// affected by mask settings
	void CopyVRam( uint32_t srcX, uint32_t srcY, uint32_t destX, uint32_t destY, uint32_t width, uint32_t height );

private:
	Timers& m_timers;
	InterruptControl& m_interruptControl;
	Renderer& m_renderer;
	CycleScheduler& m_cycleScheduler;

	FifoBuffer<uint32_t, 16> m_commandBuffer;
	uint32_t m_remainingParamaters = 0;
	CommandFunction m_commandFunction = nullptr;
	GP0Function m_gp0Mode = nullptr;

	uint32_t m_gpuRead = 0;
	GpuReadFunction m_gpuReadMode = nullptr;

	Status m_status;

	// draw mode
	bool m_texturedRectFlipX = false;
	bool m_texturedRectFlipY = false;

	// texture window
	uint8_t m_textureWindowMaskX = 0;
	uint8_t m_textureWindowMaskY = 0;
	uint8_t m_textureWindowOffsetX = 0;
	uint8_t m_textureWindowOffsetY = 0;

	// drawing area
	uint16_t m_drawAreaLeft = 0;
	uint16_t m_drawAreaTop = 0;
	uint16_t m_drawAreaRight = 0;
	uint16_t m_drawAreaBottom = 0;

	// use SetDrawOffset(x,y) to change values
	int16_t m_drawOffsetX = 0;
	int16_t m_drawOffsetY = 0;

	// start of display area
	uint16_t m_displayAreaStartX = 0;
	uint16_t m_displayAreaStartY = 0;

	// horizontal display range
	uint16_t m_horDisplayRange1 = 0;
	uint16_t m_horDisplayRange2 = 0;

	// vertical display range
	uint16_t m_verDisplayRange1 = 0;
	uint16_t m_verDisplayRange2 = 0;

	// timing
	uint32_t m_currentScanline = 0;
	float m_currentDot = 0.0f;
	float m_dotTimerFraction = 0.0f;
	bool m_hblank = false;
	bool m_vblank = false;
	mutable uint32_t m_cachedCyclesUntilNextEvent = 0;

	bool m_displayFrame = false;

	std::unique_ptr<uint16_t[]> m_vram; // 1MB of VRAM, 1024x512

	struct VRamCopyState
	{
		uint32_t left = 0;
		uint32_t top = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t x = 0;
		uint32_t y = 0;

		// CPU -> VRAM only
		std::unique_ptr<uint16_t[]> pixelBuffer;
		bool oddWidth = false;

		bool IsFinished() const noexcept { return x == 0 && y == height; }

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

		void InitializePixelBuffer()
		{
			// buffer width must be aligned to 32bit boundary
			oddWidth = ( width % 2 != 0 );
			const auto size = ( width + oddWidth ) * height;
			pixelBuffer.reset( new uint16_t[ size ] );
		}

		void PushPixel( uint16_t pixel ) noexcept
		{
			dbExpects( pixelBuffer );
			dbExpects( !IsFinished() );

			const auto index = y * ( width + oddWidth ) + x;
			pixelBuffer[ index ] = pixel;

			Increment();
		}
	};

	std::optional<VRamCopyState> m_vramCopyState;
};

}