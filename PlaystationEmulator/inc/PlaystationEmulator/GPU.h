#pragma once

#include "assert.h"
#include "Memory.h"

#include <cstdint>

namespace PSX
{

class Renderer;

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

	Gpu( Renderer& renderer ) : m_renderer{ renderer } {}

	void Reset();

	void WriteGP0( uint32_t value ) noexcept
	{
		std::invoke( m_gp0Mode, this, value );
	}

	void WriteGP1( uint32_t value ) noexcept;

	uint32_t GpuRead() const noexcept
	{
		return m_gpuRead;
	}

	uint32_t GpuStatus() const noexcept
	{
		return m_status.value

			// return vertical resolution as 240 so that the BIOS doesn't infinite loop waiting for even/odd line flag to change
			// TODO: revert when status bit 31 is implemented
			& ~( 1u << 19 );
	}

	uint32_t GetHorizontalResolution() const noexcept;

	uint32_t GetVerticalResolution() const noexcept
	{
		return ( m_status.verticalResolution && m_status.verticalInterlace ) ? 480 : 240;
	}

private:
	using GP0Function = void( Gpu::* )( uint32_t ) noexcept;

	using CommandFunction = void( Gpu::* )( ) noexcept;

	struct CommandBuffer
	{
	public:
		static constexpr uint32_t MaxSize = 16;

		void Push( uint32_t value ) noexcept
		{
			dbExpects( m_size < MaxSize );
			m_buffer[ m_size++ ] = value;
		}

		const uint32_t& operator[]( size_t index ) const noexcept
		{
			dbExpects( index < m_size );
			return m_buffer[ index ];
		}

		size_t Size() const noexcept { return m_size; }

		void Clear() { m_size = 0; }

	private:
		uint32_t m_buffer[ MaxSize ];
		uint32_t m_size = 0;
	};

	void ClearCommandBuffer()
	{
		m_commandBuffer.Clear();
		m_remainingWords = 0;
		m_commandFunction = nullptr;

		m_gp0Mode = &Gpu::GP0Command;
	}

	void InitCommand( uint32_t command, uint32_t remainingWords, CommandFunction function )
	{
		dbExpects( m_commandBuffer.Size() == 0 );
		m_commandBuffer.Push( command );
		m_remainingWords = remainingWords;
		m_commandFunction = function;

		m_gp0Mode = &Gpu::GP0Params;
	}

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

	void TempFinishCommandParams() noexcept
	{
		ClearCommandBuffer();
	}

private:
	Renderer& m_renderer;

	CommandBuffer m_commandBuffer;
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
	void SetDrawOffset( int16_t x, int16_t y );

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