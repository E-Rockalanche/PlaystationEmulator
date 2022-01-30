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

	float GetRefreshRate() const noexcept;
	float GetAspectRatio() const noexcept;

	bool GetDisplayFrame() const noexcept { return m_crtState.displayFrame; }
	void ResetDisplayFrame() noexcept { m_crtState.displayFrame = false; }

	void UpdateCrtEventEarly();
	void ScheduleCrtEvent() noexcept;

private:
	struct CrtConstants
	{
		uint16_t totalScanlines;
		uint16_t cyclesPerScanline;
		uint16_t visibleScanlineStart;
		uint16_t visibleScanlineEnd;
		uint16_t visibleCycleStart;
		uint16_t visibleCycleEnd;
	};

	// values from https://problemkaputt.de/psx-spx.htm#gputimings
	static constexpr CrtConstants NTSCConstants{ 263, 3413, 16, 256, 488, 3288 };
	static constexpr CrtConstants PALConstants{ 314, 3406, 20, 308, 487, 3282 };

	static constexpr size_t DotTimerIndex = 0;
	static constexpr size_t HBlankTimerIndex = 1;

	static constexpr cycles_t MaxRunAheadCommandCycles = 128;

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
			// draw mode:
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
		struct
		{
			uint32_t : 16;
			uint32_t horizontalResolution : 3; // 256, 368, 320, 368, 512, 368, 640, 368
			uint32_t : 13;
		};
		uint32_t value = 0;

		static constexpr uint32_t TexPageMask = 0x00009ff;

		void SetTexPage( TexPage texPage ) noexcept
		{
			stdx::masked_set<uint32_t>( value, TexPageMask, texPage.value );
			textureDisable = texPage.textureDisable;
		}

		TexPage GetTexPage() const noexcept
		{
			return TexPage{ static_cast<uint16_t>( value & TexPageMask ) };
		}

		uint16_t GetCheckMask() const noexcept { return static_cast<uint16_t>( checkMaskOnDraw << 15 ); }
		uint16_t GetSetMask() const noexcept { return static_cast<uint16_t>( setMaskOnDraw << 15 ); }

		SemiTransparencyMode GetSemiTransparencyMode() const noexcept { return static_cast<SemiTransparencyMode>( semiTransparencyMode ); }
		DisplayAreaColorDepth GetDisplayAreaColorDepth() const noexcept { return static_cast<DisplayAreaColorDepth>( displayAreaColorDepth ); }

		DmaDirection GetDmaDirection() const noexcept { return static_cast<DmaDirection>( dmaDirection ); }

		bool Is480iMode() const noexcept { return verticalResolution && verticalInterlace; }

		bool SkipDrawingToActiveInterlaceFields() const noexcept
		{
			return Is480iMode() && !drawToDisplayArea;
		}
	};
	static_assert( sizeof( Status ) == 4 );

	using CommandFunction = void( Gpu::* )( ) noexcept;

private:
	// rounded down, remainder is stored in fractionalCycles
	static constexpr cycles_t ConvertCpuToGpuCycles( cycles_t cpuCycles, cycles_t& fractionalCycles ) noexcept
	{
		const auto multiplied = cpuCycles * 11 + fractionalCycles;
		fractionalCycles = multiplied % 7;
		return multiplied / 7;
	}

	// rounded down
	static constexpr cycles_t ConvertCpuToGpuCycles( cycles_t cpuCycles ) noexcept
	{
		return ( cpuCycles * 11 ) / 7;
	}

	// rounded up so we don't undershoot conversion from CPU to GPU cycles
	static constexpr cycles_t ConvertGpuToCpuCycles( cycles_t gpuCycles, cycles_t fractionalCycles = 0 ) noexcept
	{
		return ( gpuCycles * 7 - fractionalCycles + 10 ) / 11;
	}

	void SoftReset() noexcept;

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

	inline void ClampToDrawArea( int32_t& x, int32_t& y ) const noexcept
	{
		x = std::clamp<int32_t>( x, (int32_t)m_drawAreaLeft, (int32_t)m_drawAreaRight );
		y = std::clamp<int32_t>( y, (int32_t)m_drawAreaTop, (int32_t)m_drawAreaBottom );
	}

	inline void AddTriangleCommandCycles( int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, bool textured, bool semitransparent )
	{
		// Don't worry about intersecting triangle with draw area. Just clamp coordinates. Better to unsershoot than overshoot draw timing
		ClampToDrawArea( x1, y1 );
		ClampToDrawArea( x2, y2 );
		ClampToDrawArea( x3, y3 );

		cycles_t cycles = std::abs( ( x1 * ( y2 - y3 ) + x2 * ( y3 - y1 ) + x3 * ( y1 - y2 ) ) / 2 );
		if ( textured )
			cycles *= 2;

		if ( semitransparent || m_status.checkMaskOnDraw )
			cycles += ( cycles + 1 ) / 2;

		if ( m_status.SkipDrawingToActiveInterlaceFields() )
			cycles /= 2;

		m_pendingCommandCycles += cycles;
	}

	inline void AddRectangleCommandCycles( uint32_t width, uint32_t height, bool textured, bool semitransparent )
	{
		uint32_t cyclesPerRow = static_cast<cycles_t>( width );
		if ( textured )
			cyclesPerRow *= 2;

		if ( semitransparent || m_status.checkMaskOnDraw )
			cyclesPerRow += ( width + 1 ) / 2;

		if ( m_status.SkipDrawingToActiveInterlaceFields() )
			height = std::max<uint32_t>( height / 2, 1 );

		m_pendingCommandCycles += static_cast<cycles_t>( cyclesPerRow * height );
	}

	inline void AddLineCommandCycles( uint32_t width, uint32_t height )
	{
		if ( m_status.SkipDrawingToActiveInterlaceFields() )
			height = std::max<uint32_t>( height / 2, 1 );

		m_pendingCommandCycles += static_cast<cycles_t>( std::max( width, height ) );
	}

	// GPU commands run at twice the CPU clock speed according to Duckstation

	static constexpr cycles_t ConvertCpuToCommandCycles( cycles_t cpuCycles ) noexcept
	{
		return cpuCycles * 2;
	}

	// round up so we don't undershoot conversion from CPU to command cycles
	static constexpr cycles_t ConvertCommandToCpuCycles( cycles_t commandCycles ) noexcept
	{
		return ( commandCycles + 1 ) / 2;
	}

	void UpdateCommandCycles( cycles_t cpuCycles ) noexcept;

	// command functions

	void Command_FillRectangle() noexcept;
	void Command_CopyRectangle() noexcept;
	void Command_WriteToVRam() noexcept;
	void Command_ReadFromVRam() noexcept;
	void Command_RenderPolygon() noexcept;
	void Command_RenderLine() noexcept;
	void Command_RenderPolyLine() noexcept;
	void Command_RenderLineInternal( Position p1, Color c1, Position p2, Color c2, TexPage texPage, bool semiTransparent ) noexcept;
	void Command_RenderRectangle() noexcept;

	void UpdateCrtConstants() noexcept;
	void UpdateCrtDisplay() noexcept;

	void UpdateCrtCycles( cycles_t cpuCycles ) noexcept;

private:
	InterruptControl& m_interruptControl;
	Renderer& m_renderer;
	Timers* m_timers = nullptr; // circular dependency
	Dma* m_dma = nullptr; // circular dependency

	EventHandle m_crtEvent;
	EventHandle m_commandEvent;

	State m_state = State::Idle;
	FifoBuffer<uint32_t, 16> m_commandBuffer;
	uint32_t m_remainingParamaters = 0;
	CommandFunction m_commandFunction = nullptr;
	cycles_t m_pendingCommandCycles = 0;
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
	CrtConstants m_crtConstants;

	struct CrtState
	{
		cycles_t fractionalCycles = 0;

		uint32_t scanline = 0;
		cycles_t cycleInScanline = 0;

		uint32_t dotClockDivider = 0;
		uint32_t dotFraction = 0;

		// custom visible range (based on crop mode)
		uint16_t visibleCycleStart = 0;
		uint16_t visibleCycleEnd = 0;
		uint16_t visibleScanlineStart = 0;
		uint16_t visibleScanlineEnd = 0;

		bool hblank = false;
		bool vblank = false;
		bool evenOddLine = false;
		bool displayFrame = false;
	};
	CrtState m_crtState;

	mutable cycles_t m_cachedCyclesUntilNextEvent = 0;

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

	CropMode m_cropMode = CropMode::Fit;
};

}