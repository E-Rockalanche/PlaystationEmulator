#pragma once

#include "Defs.h"

#include "FifoBuffer.h"
#include "GpuDefs.h"

#include <Math/Rectangle.h>

#include <stdx/bit.h>

#include <memory>
#include <optional>
#include <vector>

namespace PSX
{

static constexpr float CpuClockSpeed = CpuCyclesPerSecond; // Hz
static constexpr float VideoClockSpeed = CpuCyclesPerSecond * 11.0f / 7.0f; // Hz

static constexpr float RefreshRatePAL = 50.0f;
static constexpr float RefreshRateNTSC = 60.0f;

static constexpr uint32_t ScanlinesPAL = 314;
static constexpr uint32_t ScanlinesNTSC = 263;

constexpr float ConvertCpuToVideoCycles( cycles_t cycles ) noexcept
{
	return static_cast<float>( cycles ) * 11.0f / 7.0f;
}

constexpr float ConvertVideoToCpuCycles( float cycles ) noexcept
{
	return cycles * 7.0f / 11.0f;
}

class Gpu
{
public:
	Gpu( InterruptControl& interruptControl, Renderer& renderer, EventManager& eventManager );
	~Gpu();

	void SetTimers( Timers& timers ) { m_timers = &timers; }
	void SetDma( Dma& dma ) { m_dma = &dma; }

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

	void DmaIn( const uint32_t* input, uint32_t count ) noexcept;
	void DmaOut( uint32_t* output, uint32_t count ) noexcept;

	bool IsInterlaced() const noexcept { return m_status.verticalResolution && m_status.verticalInterlace; }

	uint32_t GetHorizontalResolution() const noexcept;
	uint32_t GetVerticalResolution() const noexcept { return IsInterlaced() ? 480 : 240; }

	uint32_t GetScanlines() const noexcept { return m_status.videoMode ? ScanlinesPAL : ScanlinesNTSC; }
	float GetRefreshRate() const noexcept { return m_status.videoMode ? RefreshRatePAL : RefreshRateNTSC; }

	bool GetDisplayFrame() const noexcept { return m_displayFrame; }
	void ResetDisplayFrame() noexcept { m_displayFrame = false; }

	void UpdateClockEventEarly();
	void ScheduleNextEvent();

private:
	enum class State
	{
		Idle,
		Parameters,
		WritingVRam,
		ReadingVRam,
		PolyLine
	};

	enum class DmaDirection
	{
		Off,
		Fifo,
		CpuToGp0,
		GpuReadToCpu
	};

	union Status
	{
		struct
		{
			uint32_t texturePageBaseX : 4; // N*64
			uint32_t texturePageBaseY : 1; // N*256
			uint32_t semiTransparencyMode : 2; // 0=B/2+F/2, 1=B+F, 2=B-F, 3=B+F/4
			uint32_t texturePageColors : 2; // 0=4bit, 1=8bit, 2=15bit
			uint32_t dither : 1; // 0=Off/strip LSBs, 1=Dither Enabled
			uint32_t drawToDisplayArea : 1;
			uint32_t setMaskOnDraw : 1;
			uint32_t checkMaskOnDraw : 1;
			uint32_t interlaceField : 1;
			uint32_t reverseFlag : 1;
			uint32_t textureDisable : 1; // only works on new GPUs and/or dev machines?

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
			uint32_t dmaDirection : 2; // 0=Off, 1=FIFO, 2=CPUtoGP0, 3=GPUREADtoCPU
			uint32_t evenOddVblank : 1; // 0=Even or Vblank, 1=Odd
		};
		uint32_t value = 0;

		void SetTexPage( TexPage texPage ) noexcept { stdx::masked_set<uint32_t>( value, 0x01ff, texPage.value ); }
		TexPage GetTexPage() const noexcept { return value & 0x1ff; }

		uint16_t GetCheckMask() const noexcept { return static_cast<uint16_t>( checkMaskOnDraw << 15 ); }
		uint16_t GetSetMask() const noexcept { return static_cast<uint16_t>( setMaskOnDraw << 15 ); }

		SemiTransparencyMode GetSemiTransparencyMode() const noexcept { return static_cast<SemiTransparencyMode>( semiTransparencyMode ); }
		DisplayAreaColorDepth GetDisplayAreaColorDepth() const noexcept { return static_cast<DisplayAreaColorDepth>( displayAreaColorDepth ); }

		DmaDirection GetDmaDirection() const noexcept { return static_cast<DmaDirection>( dmaDirection ); }
	};
	static_assert( sizeof( Status ) == 4 );

	using CommandFunction = void( Gpu::* )( ) noexcept;

private:
	void WriteGP0( uint32_t value ) noexcept;
	void WriteGP1( uint32_t value ) noexcept;

	uint32_t GpuRead() noexcept;
	uint32_t GpuStatus() noexcept;

	void ProcessCommandBuffer() noexcept;

	void UpdateDmaRequest() noexcept;

	void ClearCommandBuffer() noexcept;

	void InitCommand( uint32_t paramaterCount, CommandFunction function ) noexcept;

	void SetupVRamCopy() noexcept;
	void FinishVRamWrite() noexcept;

	void ExecuteCommand() noexcept;

	void EndCommand() noexcept
	{
		m_state = State::Idle;
		m_remainingParamaters = 0;
	}

	void UpdateCommandCycles( float gpuCycles ) noexcept;

	// command functions

	void Command_FillRectangle() noexcept;
	void Command_CopyRectangle() noexcept;
	void Command_WriteToVRam() noexcept;
	void Command_ReadFromVRam() noexcept;
	void Command_RenderPolygon() noexcept;
	void Command_RenderLine() noexcept;
	void Command_RenderPolyLine() noexcept;
	void Command_RenderRectangle() noexcept;

	// CRT functions

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

	void UpdateCycles( float gpuTicks ) noexcept;

private:
	InterruptControl& m_interruptControl;
	Renderer& m_renderer;
	Timers* m_timers = nullptr; // circular dependency
	Dma* m_dma = nullptr; // circular dependency
	EventHandle m_clockEvent;
	EventHandle m_commandEvent;

	State m_state = State::Idle;
	FifoBuffer<uint32_t, 16> m_commandBuffer;
	uint32_t m_remainingParamaters = 0;
	CommandFunction m_commandFunction = nullptr;
	float m_pendingCommandCycles = 0;
	bool m_processingCommandBuffer = false;

	uint32_t m_gpuRead = 0;

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
	uint16_t m_horDisplayRangeStart = 0;
	uint16_t m_horDisplayRangeEnd = 0;

	// vertical display range
	uint16_t m_verDisplayRangeStart = 0;
	uint16_t m_verDisplayRangeEnd = 0;

	// timing
	uint32_t m_currentScanline = 0;
	float m_currentDot = 0.0f;
	float m_dotTimerFraction = 0.0f;
	bool m_hblank = false;
	bool m_vblank = false;
	bool m_drawingEvenOddLine = false;

	mutable cycles_t m_cachedCyclesUntilNextEvent = 0;

	bool m_displayFrame = false;

	std::unique_ptr<uint16_t[]> m_vram; // 1MB of VRAM, 1024x512, used for VRAM to CPU transfers

	std::vector<uint32_t> m_transferBuffer; // for VRAM write and polyline commands

	struct VRamTransferState
	{
		uint32_t left = 0;
		uint32_t top = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t dx = 0;
		uint32_t dy = 0;

		bool IsFinished() const noexcept { return dx == 0 && dy == height; }

		uint32_t GetWrappedX() const noexcept { return ( left + dx ) % VRamWidth; }

		uint32_t GetWrappedY() const noexcept { return ( top + dy ) % VRamHeight; }

		void Increment() noexcept
		{
			if ( ++dx == width )
			{
				dx = 0;
				++dy;
			}
		}
	};
	std::optional<VRamTransferState> m_vramTransferState;
};

}