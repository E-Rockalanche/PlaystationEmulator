#include "GPU.h"

#include "CycleScheduler.h"
#include "InterruptControl.h"
#include "Renderer.h"
#include "Timers.h"

#include <stdx/assert.h>
#include <stdx/bit.h>

namespace PSX
{

namespace
{

constexpr std::pair<uint16_t, uint16_t> DecodeFillPosition( uint32_t gpuParam ) noexcept
{
	const uint16_t x = static_cast<uint16_t>( gpuParam ) & 0x3f0;
	const uint16_t y = static_cast<uint16_t>( gpuParam >> 16 ) & 0x1ff;
	return { x, y };
}

constexpr std::pair<uint16_t, uint16_t> DecodeFillSize( uint32_t gpuParam ) noexcept
{
	const uint16_t w = ( ( static_cast<uint16_t>( gpuParam ) & 0x3ff ) + 0x0f ) & ~0x0f;
	const uint16_t h = static_cast<uint16_t>( gpuParam >> 16 ) & 0x1ff;
	return { w, h };
}

constexpr std::pair<uint16_t, uint16_t> DecodeCopyPosition( uint32_t gpuParam ) noexcept
{
	const uint16_t x = static_cast<uint16_t>( gpuParam ) & 0x3ff;
	const uint16_t y = static_cast<uint16_t>( gpuParam >> 16 ) & 0x1ff;
	return { x, y };
}

constexpr std::pair<uint16_t, uint16_t> DecodeCopySize( uint32_t gpuParam ) noexcept
{
	const uint16_t w = ( ( static_cast<uint16_t>( gpuParam ) - 1 ) & 0x3ff ) + 1;
	const uint16_t h = ( ( static_cast<uint16_t>( gpuParam >> 16 ) - 1 ) & 0x1ff ) + 1;
	return { w, h };
}

struct RenderCommand
{
	enum : uint32_t
	{
		TextureMode = 1u << 24, // textured polygon/rect only (0=blended, 1=raw)
		SemiTransparency = 1u << 25, // all render types
		TextureMapping = 1u << 26, // polygon/rect only
		RectSizeMask = 0x3u << 27, // rect only
		NumVertices = 1u << 27, // polygon only (0=3, 1=4)
		NumLines = 1u << 27, // line only (0=1, 1=poly?)
		Shading = 1u << 28, // polygon/line only
		PrimitiveTypeMask = 0x7u << 29
	};

	static constexpr uint32_t GetVertices( uint32_t command )
	{
		return ( command & NumVertices ) ? 4 : 3;
	}
};

enum class RectangleSize
{
	Variable,
	One,
	Eight,
	Sixteen,
};

enum class PrimitiveType
{
	Polygon = 1,
	Line = 2,
	Rectangle = 3,
};

/*
struct Vertex
{
	Vertex( uint32_t value )
		: x{ static_cast<int16_t>( value ) }
		, y{ static_cast<int16_t>( value >> 16 ) }
	{
		// top 5 bits of x and y should be only be sign extension
		dbEnsures( -1024 <= x && x <= 1023 );
		dbEnsures( -1024 <= y && y <= 1023 );
	}

	int16_t x;
	int16_t y;
};

struct Color
{
	Color( uint32_t value )
		: r{ static_cast<uint8_t>( value ) }
		, g{ static_cast<uint8_t>( value >> 8 ) }
		, b{ static_cast<uint8_t>( value >> 16 ) }
	{}

	uint8_t r;
	uint8_t g;
	uint8_t b;
};
*/

}


Gpu::Gpu( Timers& timers, InterruptControl& interruptControl, Renderer& renderer, CycleScheduler& cycleScheduler )
	: m_timers{ timers }
	, m_interruptControl{ interruptControl }
	, m_renderer{ renderer }
	, m_cycleScheduler{ cycleScheduler }
	, m_vram{ std::make_unique<uint16_t[]>( VRamWidth * VRamHeight ) } // 1MB of VRAM
{
	m_cycleScheduler.Register(
		[this]( uint32_t cycles ) { UpdateTimers( cycles ); },
		[this] { return GetCpuCyclesUntilEvent(); } );
}

void Gpu::Reset()
{
	// reset buffer, remaining words, command function, and gp0 mode
	ClearCommandBuffer();

	m_gpuRead = 0;
	m_gpuReadMode = &Gpu::GpuRead_Normal;

	m_status.value = 0;
	m_status.displayDisable = true;
	m_status.readyToReceiveCommand = true;
	m_status.readyToReceiveDmaBlock = true;

	m_renderer.SetSemiTransparency( SemiTransparency::Blend );

	m_texturedRectFlipX = false;
	m_texturedRectFlipY = false;

	m_textureWindowMaskX = 0;
	m_textureWindowMaskY = 0;
	m_textureWindowOffsetX = 0;
	m_textureWindowOffsetY = 0;

	m_drawAreaLeft = 0;
	m_drawAreaTop = 0;
	m_drawAreaRight = 0;
	m_drawAreaBottom = 0;
	m_renderer.SetDrawArea( 0, 0, 0, 0 );

	m_drawOffsetX = 0;
	m_drawOffsetY = 0;
	m_renderer.SetOrigin( 0, 0 );

	m_displayAreaStartX = 0;
	m_displayAreaStartY = 0;
	m_renderer.SetDisplayStart( 0, 0 );

	m_horDisplayRange1 = 0;
	m_horDisplayRange2 = 0;

	m_verDisplayRange1 = 0;
	m_verDisplayRange2 = 0;

	m_currentScanline = 0;
	m_currentDot = 0.0f;
	m_dotTimerFraction = 0.0f;
	m_hblank = false;
	m_vblank = false;

	m_displayFrame = false;

	// clear VRAM
	std::fill_n( m_vram.get(), VRamWidth * VRamHeight, uint16_t{ 0 } ); // , uint16_t{ 0x801f } );
	m_renderer.UpdateVRam( 0, 0, VRamWidth, VRamHeight, m_vram.get() );

	m_vramCopyState = std::nullopt;

	m_renderer.SetDisplaySize( GetHorizontalResolution(), GetVerticalResolution() );
}

void Gpu::ClearCommandBuffer() noexcept
{
	m_commandBuffer.Clear();
	m_remainingParamaters = 0;
	m_commandFunction = nullptr;
	SetGP0Mode( &Gpu::GP0_Command );
}

void Gpu::InitCommand( uint32_t command, uint32_t paramaterCount, CommandFunction function ) noexcept
{
	dbExpects( m_commandBuffer.Empty() );
	dbExpects( paramaterCount > 0 );
	dbExpects( function );
	m_commandBuffer.Push( command );
	m_remainingParamaters = paramaterCount;
	m_commandFunction = function;
	SetGP0Mode( &Gpu::GP0_Params );
}

void Gpu::SetupVRamCopy() noexcept
{
	dbExpects( !m_vramCopyState.has_value() ); // already doing a copy!
	m_vramCopyState.emplace();

	m_commandBuffer.Pop(); // pop command
	std::tie( m_vramCopyState->left, m_vramCopyState->top ) = DecodeCopyPosition( m_commandBuffer.Pop() );
	std::tie( m_vramCopyState->width, m_vramCopyState->height ) = DecodeCopySize( m_commandBuffer.Pop() );
}

void Gpu::FinishVRamTransfer() noexcept
{
	dbExpects( m_vramCopyState.has_value() );
	dbExpects( m_vramCopyState->pixelBuffer );

	if ( m_vramCopyState->IsFinished() )
	{
		// copy pixels to textures
		m_renderer.UpdateVRam(
			m_vramCopyState->left, m_vramCopyState->top,
			m_vramCopyState->width, m_vramCopyState->height,
			m_vramCopyState->pixelBuffer.get() );
	}
	else
	{
		dbBreakMessage( "CPU to VRAM pixel transfer did not finish" );
		// TODO: partial transfer
	}
}

uint32_t Gpu::GetHorizontalResolution() const noexcept
{
	if ( m_status.horizontalResolution2 )
		return 368;

	switch ( m_status.horizontalResolution1 )
	{
		case 0:	return 256;
		case 1:	return 320;
		case 2:	return 512;
		case 3:	return 640;
	}

	dbBreak();
	return 0;
}

void Gpu::GP0_Command( uint32_t value ) noexcept
{
	const uint8_t opcode = static_cast<uint8_t>( value >> 24 );
	switch ( opcode )
	{
		case 0xe1: // draw mode setting
		{
			dbLog( "Gpu::GP0_Command() -- set draw mode [%X]", value );

			static constexpr uint32_t WriteMask = 0x3f;
			stdx::masked_set( m_status.value, WriteMask, value );

			m_status.textureDisable = stdx::any_of<uint32_t>( value, 1 << 11 );

			m_texturedRectFlipX = stdx::any_of<uint32_t>( value, 1 << 12 );
			m_texturedRectFlipY = stdx::any_of<uint32_t>( value, 1 << 13 );
			break;
		}

		case 0xe2: // texture window setting
		{
			dbLog( "Gpu::GP0_Command() -- set texture window [%X]", value );

			m_textureWindowMaskX = value & 0x1f;
			m_textureWindowMaskY = ( value >> 5 ) & 0x1f;
			m_textureWindowOffsetX = ( value >> 10 ) & 0x1f;
			m_textureWindowOffsetY = ( value >> 15 ) & 0x1f;
			break;
		}

		case 0xe3: // set draw area top-left
		{
			m_drawAreaLeft = value & 0x3ff;
			m_drawAreaTop = ( value >> 10 ) & 0x1ff;

			dbLog( "Gpu::GP0_Command() -- set draw area top-left [%u, %u]", m_drawAreaLeft, m_drawAreaTop );

			m_renderer.SetDrawArea( m_drawAreaLeft, m_drawAreaTop, m_drawAreaRight, m_drawAreaBottom );

			// TODO: does this affect blanking?
			break;
		}

		case 0xe4: // set draw area bottom-right
		{
			m_drawAreaRight = value & 0x3ff;
			m_drawAreaBottom = ( value >> 10 ) & 0x1ff;

			dbLog( "Gpu::GP0_Command() -- set draw area bottom-right [%u, %u]", m_drawAreaRight, m_drawAreaBottom );

			m_renderer.SetDrawArea( m_drawAreaLeft, m_drawAreaTop, m_drawAreaRight, m_drawAreaBottom );

			// TODO: does this affect blanking?
			break;
		}

		case 0xe5: // set drawing offset
		{
			auto signExtend = []( uint32_t value )
			{
				const bool sign = value & 0x400;
				return static_cast<int16_t>( sign ? ( value | 0xfffff800 ) : value );
			};

			m_drawOffsetX = signExtend( value & 0x7ff );
			m_drawOffsetY = signExtend( ( value >> 11 ) & 0x7ff );
			dbLog( "Gpu::GP0_Command() -- set draw offset [%u, %u]", m_drawOffsetX, m_drawOffsetY );

			m_renderer.SetOrigin( m_drawOffsetX, m_drawOffsetY );
			break;
		}

		case 0xe6: // mask bit setting
		{
			dbLog( "Gpu::GP0_Command() -- set mask bits [%X]", value );

			m_status.setMaskOnDraw = value & 1;
			m_status.checkMaskOnDraw = ( value >> 1 ) & 1;
			break;
		}

		case 0x01: // clear cache
			dbLog( "Gpu::GP0_Command() -- clear GPU cache" );
			break;

		case 0x02: // fill rectangle in VRAM
			dbLog( "Gpu::GP0_Command() -- fill rectangle in VRAM" );
			InitCommand( value, 2, &Gpu::FillRectangle );
			break;

		case 0x80: // copy rectangle (VRAM to VRAM)
			dbLog( "Gpu::GP0_Command() -- copy rectangle (VRAM to VRAM)" );
			InitCommand( value, 3, &Gpu::CopyRectangle );
			break;

		case 0xa0: // copy rectangle (CPU to VRAM)
			dbLog( "Gpu::GP0_Command() -- copy rectangle (CPU to VRAM)" );
			InitCommand( value, 2, &Gpu::CopyRectangleToVram );
			break;

		case 0xc0: // copy rectangle (VRAM to CPU)
			dbLog( "Gpu::GP0_Command() -- copy rectangle (VRAM to CPU)" );
			InitCommand( value, 2, &Gpu::CopyRectangleFromVram );
			break;

		case 0x1f: // interrupt request
		{
			dbLog( "Gpu::GP0_Command() -- request interrupt" );
			if ( !m_status.interruptRequest ) // edge triggered
			{
				m_status.interruptRequest = true;
				m_interruptControl.SetInterrupt( Interrupt::Gpu );
			}
			break;
		}

		case 0x03: // unknown. Takes up space in FIFO
			dbBreak();
			break;

		case 0x00:
		case 0x04:
		case 0x1e:
		case 0xe0:
		case 0xe7:
		case 0xef:
			break; // NOP

		default:
		{
			switch ( static_cast<PrimitiveType>( ( value & RenderCommand::PrimitiveTypeMask ) >> 29 ) )
			{
				case PrimitiveType::Polygon:
				{
					const bool quad = value & RenderCommand::NumVertices;
					const bool textureMapping = value & RenderCommand::TextureMapping;
					const bool shading = value & RenderCommand::Shading;

					uint32_t params = quad ? 4 : 3;
					params *= 1 + textureMapping + shading;
					params -= shading; // first color for shaded polygons is in command

					InitCommand( value, params, &Gpu::RenderPolygon );
					break;
				}

				case PrimitiveType::Line:
				{
					m_commandBuffer.Push( value );
					m_commandFunction = &Gpu::TempFinishCommandParams;
					if ( value & RenderCommand::NumLines )
					{
						SetGP0Mode( &Gpu::GP0_PolyLine );
					}
					else
					{
						m_remainingParamaters = ( value & RenderCommand::Shading ) ? 3 : 2;
						SetGP0Mode( &Gpu::GP0_Params );
					}

					break;
				}

				case PrimitiveType::Rectangle:
				{
					const auto rectSize = static_cast<RectangleSize>( ( value & RenderCommand::RectSizeMask ) >> 27 );
					const bool textureMapping = value & RenderCommand::TextureMapping;

					const uint32_t params = 1 + ( rectSize == RectangleSize::Variable ) + textureMapping;
					InitCommand( value, params, &Gpu::RenderRectangle );
					break;
				}

				default:
					dbBreakMessage( "Gpu::GP0_Command() -- invalid GP0 opcode [%X]", opcode );
					break;
			}
			break;
		}
	}
}

void Gpu::GP0_Params( uint32_t param ) noexcept
{
	dbExpects( m_remainingParamaters > 0 );
	m_commandBuffer.Push( param );
	if ( --m_remainingParamaters == 0 )
	{
		std::invoke( m_commandFunction, this );

		// command function must clear buffer and set GP0 mode
	}
}

void Gpu::GP0_PolyLine( uint32_t param ) noexcept
{
	static constexpr uint32_t TerminationCode = 0x50005000; // should use 0x55555555, but Wild Arms 2 uses 0x50005000
	if ( stdx::all_of( param, TerminationCode ) )
	{
		std::invoke( m_commandFunction, this );
		ClearCommandBuffer();
	}
	else
	{
		m_commandBuffer.Push( param );
	}
}

void Gpu::GP0_Image( uint32_t param ) noexcept
{
	dbExpects( m_vramCopyState.has_value() );
	dbExpects( !m_vramCopyState->IsFinished() );

	const uint16_t checkMask = m_status.GetCheckMask();
	const uint16_t setMask = m_status.GetSetMask();

	auto copyPixel = [&]( uint16_t pixel )
	{
		const auto x = m_vramCopyState->GetWrappedX();
		const auto y = m_vramCopyState->GetWrappedY();

		uint16_t* destPixel = m_vram.get() + y * VRamWidth + x;
		if ( ( *destPixel & checkMask ) == 0 )
			*destPixel = pixel | setMask;

		m_vramCopyState->Increment();
	};

	m_vramCopyState->PushPixel( static_cast<uint16_t>( param ) );

	if ( !m_vramCopyState->IsFinished() )
		m_vramCopyState->PushPixel( static_cast<uint16_t>( param >> 16 ) );

	if ( m_vramCopyState->IsFinished() )
	{
		FinishVRamTransfer();

		m_vramCopyState = std::nullopt;
		ClearCommandBuffer();
	}
}

uint32_t Gpu::GpuRead_Normal() noexcept
{
	return m_gpuRead;
}

uint32_t Gpu::GpuRead_Image() noexcept
{
	dbExpects( m_vramCopyState.has_value() );
	dbExpects( !m_vramCopyState->IsFinished() );

	auto getPixel = [this]
	{
		const auto x = m_vramCopyState->GetWrappedX();
		const auto y = m_vramCopyState->GetWrappedY();
		m_vramCopyState->Increment();
		return *( m_vram.get() + y * VRamWidth + x );
	};

	m_gpuRead = getPixel();

	if ( !m_vramCopyState->IsFinished() )
		m_gpuRead |= getPixel() << 16;

	if ( m_vramCopyState->IsFinished() )
	{
		m_gpuReadMode = &Gpu::GpuRead_Normal;
		m_status.readyToReceiveCommand = true;
		m_status.readyToReceiveDmaBlock = true;
		m_status.readyToSendVRamToCpu = false;
		m_vramCopyState = std::nullopt;
	}

	return m_gpuRead;
}

void Gpu::WriteGP1( uint32_t value ) noexcept
{
	const uint8_t opcode = static_cast<uint8_t>( value >> 24 );
	switch ( opcode )
	{
		case 0x00: // soft reset GPU
		{
			dbLog( "Gpu::WriteGP1() -- soft reset" );

			if ( m_gp0Mode == &Gpu::GP0_Image )
				FinishVRamTransfer();

			m_vramCopyState = std::nullopt;

			m_status.texturePageBaseX = 0;
			m_status.texturePageBaseY = 0;
			m_status.semiTransparency = 0;
			m_status.texturePageColors = 0;
			m_status.dither = false;
			m_status.drawToDisplayArea = false;
			m_status.textureDisable = false;
			m_status.interruptRequest = false;
			m_status.displayDisable = true;
			m_status.dmaDirection = static_cast<uint32_t>( DmaDirection::Off );
			m_status.setMaskOnDraw = false;
			m_status.checkMaskOnDraw = false;

			m_texturedRectFlipX = false;
			m_texturedRectFlipY = false;

			m_textureWindowMaskX = 0;
			m_textureWindowMaskY = 0;
			m_textureWindowOffsetX = 0;
			m_textureWindowOffsetY = 0;

			// TODO: does this affect hblanking?
			m_drawAreaLeft = 0;
			m_drawAreaTop = 0;
			m_drawAreaRight = 0;
			m_drawAreaBottom = 0;
			m_renderer.SetDrawArea( 0, 0, 0, 0 );

			m_drawOffsetX = 0;
			m_drawOffsetY = 0;
			m_renderer.SetOrigin( 0, 0 );

			m_horDisplayRange1 = 0x200;
			m_horDisplayRange2 = 0x200 + 256 * 10;
			m_verDisplayRange1 = 0x10;
			m_verDisplayRange2 = 0x10 + 240;

			m_displayAreaStartX = 0;
			m_displayAreaStartY = 0;
			m_renderer.SetDisplayStart( 0, 0 );

			ClearCommandBuffer();

			// TODO: clear texture cache?

			break;
		}

		case 0x01: // reset command buffer
			// dbLog( "Gpu::WriteGP1() -- clear command buffer" );
			ClearCommandBuffer();
			break;

		case 0x02: // ack GPU interrupt
			dbLog( "Gpu::WriteGP1() -- acknowledge interrupt" );
			m_status.interruptRequest = false;
			break;

		case 0x03: // display enable
			m_status.displayDisable = value & 1;
			dbLog( "Gpu::WriteGP1() -- enable display: %s", m_status.displayDisable ? "false" : "true" );
			break;

		case 0x04: // DMA direction / data request
		{
			m_status.dmaDirection = value & 0x3;
			dbLog( "Gpu::WriteGP1() -- set DMA direction: %u", m_status.dmaDirection );
			break;
		}

		case 0x05: // start of display area
		{
			m_displayAreaStartX = value & 0x3fe;
			m_displayAreaStartY = ( value >> 10 ) & 0x1ff;
			dbLog( "Gpu::WriteGP1() -- set display area start [%u, %u]", m_displayAreaStartX, m_displayAreaStartY );
			m_renderer.SetDisplayStart( m_displayAreaStartX, m_displayAreaStartY );
			break;
		}

		case 0x06: // horizontal display range
		{
			m_horDisplayRange1 = value & 0xfff;
			m_horDisplayRange2 = ( value >> 12 ) & 0xfff;
			dbLog( "Gpu::WriteGP1() -- set horizontal display range [%u, %u]", m_horDisplayRange1, m_horDisplayRange2 );
			break;
		}

		case 0x07: // vertical display range
		{
			m_verDisplayRange1 = value & 0x3ff;
			m_verDisplayRange2 = ( value >> 10 ) & 0x3ff;
			dbLog( "Gpu::WriteGP1() -- set vertical display range [%u, %u]", m_verDisplayRange1, m_verDisplayRange2 );
			break;
		}

		case 0x08: // display mode
		{
			dbLog( "Gpu::WriteGP1() -- set display mode [%X]", value );

			static constexpr uint32_t WriteMask = 0x3f << 17;
			Status newStatus;
			newStatus.value = ( m_status.value & ~WriteMask ) | ( ( value << 17 ) & WriteMask );
			newStatus.horizontalResolution2 = ( value >> 6 ) & 1;
			newStatus.reverseFlag = ( value >> 7 ) & 1;

			// update cycles and renderer if the new status is different
			if ( newStatus.value != m_status.value )
			{
				m_cycleScheduler.UpdateEarly();
				m_status.value = newStatus.value;
				m_cycleScheduler.ScheduleNextUpdate();
				m_renderer.SetDisplaySize( GetHorizontalResolution(), GetVerticalResolution() );
			}
			break;
		}

		case 0x10: // get GPU info
		{
			switch ( value % 8 )
			{
				default:
					break; // return nothing

				case 2: // return texture window setting
				{
					m_gpuRead = m_textureWindowMaskX |
						( m_textureWindowMaskY << 5 ) |
						( m_textureWindowOffsetX << 10 ) |
						( m_textureWindowOffsetY << 15 );
					break;
				}

				case 3: // return draw area top-left
				{
					m_gpuRead = m_drawAreaLeft | ( m_drawAreaTop << 10 );
					break;
				}

				case 4: // return draw area bottom-right
				{
					m_gpuRead = m_drawAreaRight | ( m_drawAreaBottom << 10 );
					break;
				}

				case 5: // return draw offset
				{
					m_gpuRead = ( static_cast<uint32_t>( m_drawOffsetX ) & 0x7ff ) |
						( ( static_cast<uint32_t>( m_drawOffsetY ) & 0x7ff ) << 11 );
					break;
				}
			}
			break;
		}

		default:
			dbBreakMessage( "unhandled GP1 opcode [%x]", opcode );
			break;
	}
}

uint32_t Gpu::GpuStatus() noexcept
{
	// update timers if it could affect the even/odd bit
	const auto dotAfterCycles = m_currentDot + m_cycleScheduler.GetCycles() * GetDotsPerVideoCycle();
	if ( dotAfterCycles >= GetDotsPerScanline() )
	{
		m_cycleScheduler.UpdateEarly();
		m_cycleScheduler.ScheduleNextUpdate();
	}

	// no logging since GpuStatus is called very often
	return m_status.value & ~( static_cast<uint32_t>( m_vblank ) << 31 );
}

void Gpu::FillRectangle() noexcept
{
	// not affected by mask settings

	const Color color{ m_commandBuffer.Pop() };
	auto[ x, y ] = DecodeFillPosition( m_commandBuffer.Pop() );
	auto[ width, height ] = DecodeFillSize( m_commandBuffer.Pop() );

	dbLog( "Gpu::FillRectangle() -- pos: %u,%u size: %u,%u", x, y, width, height );

	if ( width > 0 && height > 0 )
	{
		// TODO: figure out the real conversion of RGB8 to RGB555
		const uint16_t pixel = ( color.r >> 3 ) | ( ( color.g >> 3 ) << 5 ) | ( ( color.b >> 3 ) << 10 );
		FillVRam( x, y, width, height, pixel );
	}

	ClearCommandBuffer();
}

void Gpu::CopyRectangle() noexcept
{
	// affected by mask settings

	m_commandBuffer.Pop(); // pop command
	auto[ srcX, srcY ] = DecodeCopyPosition( m_commandBuffer.Pop() );
	auto[ destX, destY ] = DecodeCopyPosition( m_commandBuffer.Pop() );
	auto[ width, height ] = DecodeCopySize( m_commandBuffer.Pop() );

	dbLog( "Gpu::CopyRectangle() -- srcPos: %u,%u destPos: %u,%u size: %u,%u", srcX, srcY, destX, destY, width, height );

	CopyVRam( srcX, srcY, destX, destY, width, height );

	ClearCommandBuffer();
}

void Gpu::CopyRectangleToVram() noexcept
{
	// affected by mask settings
	SetupVRamCopy();
	dbLog( "Gpu::CopyRectangleToVram() -- pos: %u,%u size: %u,%u", m_vramCopyState->left, m_vramCopyState->top, m_vramCopyState->width, m_vramCopyState->height );
	
	m_vramCopyState->InitializePixelBuffer();
	SetGP0Mode( &Gpu::GP0_Image );
}

void Gpu::CopyRectangleFromVram() noexcept
{
	SetupVRamCopy();
	dbLog( "Gpu::CopyRectangleFromVram() -- pos: %u,%u size: %u,%u", m_vramCopyState->left, m_vramCopyState->top, m_vramCopyState->width, m_vramCopyState->height );
	m_gpuReadMode = &Gpu::GpuRead_Image;
	m_status.readyToReceiveCommand = false;
	m_status.readyToReceiveDmaBlock = false;
	m_status.readyToSendVRamToCpu = true;
	ClearCommandBuffer(); // TODO: clear buffer here after image copy?

	// read vram from frame buffer
	m_renderer.ReadVRam( m_vram.get() );
}

void Gpu::RenderPolygon() noexcept
{
	Vertex vertices[ 4 ];

	const uint32_t command = m_commandBuffer.Pop();

	const bool quad = command & RenderCommand::NumVertices;
	const bool shaded = command & RenderCommand::Shading;
	const bool textured = command & RenderCommand::TextureMapping;
	const bool noColorBlending = command & RenderCommand::TextureMode;

	// vertex 1

	if ( shaded )
		vertices[ 0 ].color = Color{ command };
	else
	{
		const Color color{ noColorBlending ? 0x808080 : command };
		for ( auto& v : vertices )
			v.color = color;
	}

	vertices[ 0 ].position = Position{ m_commandBuffer.Pop() };

	if ( textured )
	{
		const auto value = m_commandBuffer.Pop();
		vertices[ 0 ].texCoord = TexCoord{ value };

		const auto clut = static_cast<uint16_t>( value >> 16 );
		for ( auto& v : vertices )
			v.clut = clut;
	}

	// vertex 2

	if ( shaded )
		vertices[ 1 ].color = Color{ m_commandBuffer.Pop() };

	vertices[ 1 ].position = Position{ m_commandBuffer.Pop() };

	if ( textured )
	{
		const auto value = m_commandBuffer.Pop();
		vertices[ 1 ].texCoord = TexCoord{ value };

		const auto drawMode = static_cast<uint16_t>( value >> 16 );
		for ( auto& v : vertices )
			v.drawMode = drawMode;
	}

	// vetex 3 and 4

	const size_t numVertices = 3u + quad;

	for ( size_t i = 2; i < numVertices; ++i )
	{
		if ( shaded )
			vertices[ i ].color = Color{ m_commandBuffer.Pop() };

		vertices[ i ].position = Position{ m_commandBuffer.Pop() };

		if ( textured )
			vertices[ i ].texCoord = TexCoord{ m_commandBuffer.Pop() };
	}

	// TODO: check for large polygons

	const bool semiTransparent = command & RenderCommand::SemiTransparency;

	m_renderer.PushTriangle( vertices, semiTransparent );
	if ( quad )
		m_renderer.PushTriangle( vertices + 1, semiTransparent );

	ClearCommandBuffer();
}

void Gpu::RenderRectangle() noexcept
{
	// FlushVRam();

	Vertex vertices[ 4 ];

	const uint32_t command = m_commandBuffer.Pop();
	dbAssert( static_cast<PrimitiveType>( command >> 29 ) == PrimitiveType::Rectangle );

	// set position/dimensions
	const Position pos{ m_commandBuffer.Pop() };
	uint32_t width;
	uint32_t height;
	switch ( static_cast<RectangleSize>( ( command >> 27 ) & 0x3 ) )
	{
		case RectangleSize::Variable:
		{
			const uint32_t sizeParam = m_commandBuffer.Pop();
			width = sizeParam & 0xffff;
			height = sizeParam >> 16;

			// TODO: not sure if it is ignored or masked
			if ( width >= VRamWidth || height >= VRamHeight )
			{
				dbLogWarning( "Gpu::RenderRectangle -- ignoring rectangle larger than %ux%u", VRamWidth - 1, VRamHeight - 1 );
				ClearCommandBuffer();
				return;
			}

			if ( width > 256 || height > 256 )
				dbLogWarning( "Gpu::RenderRectangle -- rectangle texture needs to be tiled [%u, %u]", width, height );

			break;
		}

		case RectangleSize::One:
			width = height = 1;
			break;

		case RectangleSize::Eight:
			width = height = 8;
			break;

		case RectangleSize::Sixteen:
			width = height = 16;
			break;

		default:
			dbBreak();
			width = height = 0;
			break;
	}
	vertices[ 0 ].position = pos;
	vertices[ 1 ].position = Position{ pos.x, pos.y + static_cast<int16_t>( height ) };
	vertices[ 2 ].position = Position{ pos.x + static_cast<int16_t>( width ), pos.y };
	vertices[ 3 ].position = Position{ pos.x + static_cast<int16_t>( width ), pos.y + static_cast<int16_t>( height ) };

	// set color
	const bool noColorBlend = command & RenderCommand::TextureMode;
	const Color color{ noColorBlend ? 0xffffff : command };
	for ( auto& v : vertices )
		v.color = color;

	if ( command & RenderCommand::TextureMapping )
	{
		const uint32_t value = m_commandBuffer.Pop();

		const TexCoord topLeft{ value };
		vertices[ 0 ].texCoord = topLeft;
		vertices[ 1 ].texCoord = TexCoord{ topLeft.u, static_cast<uint16_t>( topLeft.v + height - 1 ) };
		vertices[ 2 ].texCoord = TexCoord{ static_cast<uint16_t>( topLeft.u + width - 1 ), topLeft.v };
		vertices[ 3 ].texCoord = TexCoord{ static_cast<uint16_t>( topLeft.u + width - 1 ), static_cast<uint16_t>( topLeft.v + height - 1 ) };

		const uint16_t clut = static_cast<uint16_t>( value >> 16 );
		const uint16_t drawMode = m_status.GetDrawMode();
		for ( auto& v : vertices )
		{
			v.clut = clut;
			v.drawMode = drawMode;
		}
	}

	const bool semiTransparent = command & RenderCommand::SemiTransparency;

	m_renderer.PushQuad( vertices, semiTransparent );

	ClearCommandBuffer();
}

void Gpu::UpdateTimers( uint32_t cpuTicks ) noexcept
{
	dbExpects( cpuTicks <= m_cachedCyclesUntilNextEvent );

	const float gpuTicks = ConvertCpuToVideoCycles( static_cast<float>( cpuTicks ) );
	const float dots = gpuTicks * GetDotsPerVideoCycle();

	auto& dotTimer = m_timers[ 0 ];
	if ( dotTimer.GetClockSource() % 2 )
	{
		m_dotTimerFraction += dots;
		dotTimer.Update( static_cast<uint32_t>( m_dotTimerFraction ) );
		m_dotTimerFraction = std::fmod( m_dotTimerFraction, 1.0f );
	}

	// update render position
	const auto dotsPerScanline = GetDotsPerScanline();
	m_currentDot += dots;
	while ( m_currentDot >= dotsPerScanline )
	{
		m_currentDot -= dotsPerScanline;
		m_currentScanline = ( m_currentScanline + 1 ) % GetScanlines();
		if ( !IsInterlaced() )
			m_status.drawingEvenOdd ^= 1;
	}

	// check for hblank
	const bool hblank = m_currentDot >= GetHorizontalResolution();

	if ( dotTimer.GetSyncEnable() )
		dotTimer.UpdateBlank( hblank );

	auto& hblankTimer = m_timers[ 1 ];
	if ( hblankTimer.GetClockSource() % 2 )
	{
		const uint32_t lines = static_cast<uint32_t>( dots / dotsPerScanline );
		hblankTimer.Update( static_cast<uint32_t>( hblank && !m_hblank ) + lines );
	}

	m_hblank = hblank;

	// check for vblank

	const bool vblank = m_currentScanline >= 240;

	if ( hblankTimer.GetSyncEnable() )
		hblankTimer.UpdateBlank( vblank );

	if ( !m_vblank && vblank )
	{
		dbLog( "VBLANK" );

		m_interruptControl.SetInterrupt( Interrupt::VBlank );

		m_displayFrame = true;

		if ( IsInterlaced() )
			m_status.drawingEvenOdd ^= 1;
	}
	m_vblank = vblank;
}

uint32_t Gpu::GetCpuCyclesUntilEvent() const noexcept
{
	float gpuTicks = std::numeric_limits<float>::max();

	const auto horRez = GetHorizontalResolution();

	const float dotsPerCycle = GetDotsPerVideoCycle();

	auto& dotTimer = m_timers[ 0 ];
	if ( dotTimer.GetSyncEnable() ) // dot timer synchronizes with hblanks
	{
		const float ticksUntilHblankChange = ( ( m_currentDot < horRez ? horRez : GetDotsPerScanline() ) - m_currentDot ) / dotsPerCycle;

		gpuTicks = std::min( gpuTicks, ticksUntilHblankChange );
	}

	if ( !dotTimer.GetPaused() && dotTimer.GetClockSource() % 2 )
	{
		const float ticksUntilIrq = dotTimer.GetTicksUntilIrq() / dotsPerCycle;

		gpuTicks = std::min( gpuTicks, ticksUntilIrq );
	}

	const uint32_t linesUntilVblankChange = ( m_currentScanline < 240 ? 240 : GetScanlines() ) - m_currentScanline;
	const float ticksUntilVblankChange = linesUntilVblankChange * GetVideoCyclesPerScanline() - m_currentDot / dotsPerCycle;
	gpuTicks = std::min( gpuTicks, ticksUntilVblankChange );

	auto& hblankTimer = m_timers[ 1 ];
	if ( !hblankTimer.GetPaused() && hblankTimer.GetClockSource() % 2 )
	{
		const float ticksUntilHblank = ( ( m_currentDot < horRez ? horRez : GetDotsPerScanline() + horRez ) - m_currentDot ) / dotsPerCycle;
		const float ticksUntilIrq = hblankTimer.GetTicksUntilIrq() * GetVideoCyclesPerScanline() - ticksUntilHblank;

		gpuTicks = std::min( gpuTicks, ticksUntilIrq );
	}

	const auto cpuCycles = static_cast<uint32_t>( std::ceil( ConvertVideoToCpuCycles( gpuTicks ) ) );
	dbAssert( cpuCycles > 0 );

	m_cachedCyclesUntilNextEvent = cpuCycles;

	return cpuCycles;
}

void Gpu::FillVRam( uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint16_t value )
{
	dbExpects( x < VRamWidth );
	dbExpects( y < VRamHeight );
	dbExpects( width > 0 );
	dbExpects( height > 0 );

	// wrap fill

	uint32_t width2 = 0;
	if ( x + width > VRamWidth )
	{
		width2 = x + width - VRamWidth;
		width -= width2;
	}

	uint32_t height2 = 0;
	if ( y + height > VRamHeight )
	{
		height2 = y + height - VRamHeight;
		height -= height2;
	}

	m_renderer.FillVRam( x, y, width, height, value );

	if ( width2 > 0 )
		m_renderer.FillVRam( 0, y, width2, height, value );

	if ( height2 > 0 )
		m_renderer.FillVRam( x, 0, width, height2, value );

	if ( width2 > 0 && height2 > 0 )
		m_renderer.FillVRam( 0, 0, width2, height2, value );
}

void Gpu::CopyVRam( uint32_t srcX, uint32_t srcY, uint32_t destX, uint32_t destY, uint32_t width, uint32_t height )
{
	dbExpects( srcX < VRamWidth );
	dbExpects( srcY < VRamHeight );
	dbExpects( destX < VRamWidth );
	dbExpects( destY < VRamHeight );
	dbExpects( width > 0 );
	dbExpects( height > 0 );

	// TODO wrap copy

	// TODO: check mask bits

	m_renderer.CopyVRam( srcX, srcY, width, height, destX, destY, width, height );
}

} // namespace PSX