#include "GPU.h"
#include "DMA.h"

#include "EventManager.h"
#include "InterruptControl.h"
#include "Renderer.h"
#include "Timers.h"

#include <stdx/assert.h>
#include <stdx/bit.h>

#define GPU_RENDER_POLYGONS true
#define GPU_RENDER_RECTANGLES true

namespace PSX
{

namespace
{

constexpr std::pair<uint16_t, uint16_t> DecodeFillPosition( uint32_t gpuParam ) noexcept
{
	// Horizontally the filling is done in 16-pixel (32-bytes) units
	const uint16_t x = static_cast<uint16_t>( gpuParam ) & 0x3f0;
	const uint16_t y = static_cast<uint16_t>( gpuParam >> 16 ) & VRamHeightMask;
	return { x, y };
}

constexpr std::pair<uint16_t, uint16_t> DecodeFillSize( uint32_t gpuParam ) noexcept
{
	// Horizontally the filling is done in 16-pixel (32-bytes) units
	const uint16_t w = ( ( static_cast<uint16_t>( gpuParam ) & VRamWidthMask ) + 0x0f ) & ~0x0f;
	const uint16_t h = static_cast<uint16_t>( gpuParam >> 16 ) & VRamHeightMask;
	return { w, h };
}

constexpr std::pair<uint16_t, uint16_t> DecodeCopyPosition( uint32_t gpuParam ) noexcept
{
	const uint16_t x = static_cast<uint16_t>( gpuParam ) & VRamWidthMask;
	const uint16_t y = static_cast<uint16_t>( gpuParam >> 16 ) & VRamHeightMask;
	return { x, y };
}

constexpr std::pair<uint16_t, uint16_t> DecodeCopySize( uint32_t gpuParam ) noexcept
{
	const uint16_t w = ( ( static_cast<uint16_t>( gpuParam ) - 1 ) & VRamWidthMask ) + 1;
	const uint16_t h = ( ( static_cast<uint16_t>( gpuParam >> 16 ) - 1 ) & VRamHeightMask ) + 1;
	return { w, h };
}

union RenderCommand
{
	RenderCommand() = default;
	RenderCommand( uint32_t v ) : value{ v } {}

	struct
	{
		uint32_t color : 24;

		uint32_t textureMode : 1; // textured polygon/rect only (0=blended, 1=raw)
		uint32_t semiTransparency : 1; // all render types

		uint32_t textureMapping : 1; // polygon/rect only
		uint32_t quadPolygon : 1; // polygon only (0=3, 1=4)
		uint32_t shading : 1; // polygon/line only

		uint32_t type : 3;
	};
	struct
	{
		uint32_t : 27;
		uint32_t rectSize : 2; // rect only
		uint32_t : 3;
	};
	struct
	{
		uint32_t : 27;
		uint32_t numLines : 1; // line only (0=1, 1=poly?)
		uint32_t : 4;
	};
	uint32_t value = 0;
};
static_assert( sizeof( RenderCommand ) == 4 );

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

}

Gpu::Gpu( InterruptControl& interruptControl, Renderer& renderer, EventManager& eventManager )
	: m_interruptControl{ interruptControl }
	, m_renderer{ renderer }
	, m_vram{ std::make_unique<uint16_t[]>( VRamWidth * VRamHeight ) } // 1MB of VRAM
{
	m_clockEvent = eventManager.CreateEvent( "GPU clock event", [this]( cycles_t cpuCycles )
		{
			dbExpects( cpuCycles <= m_cachedCyclesUntilNextEvent );
			UpdateCycles( ConvertCpuToVideoCycles( cpuCycles ) );
		} );
}

Gpu::~Gpu() = default;

void Gpu::Reset()
{
	m_state = State::Idle;

	m_commandBuffer.Reset();
	m_remainingParamaters = 0;
	m_commandFunction = nullptr;

	m_gpuRead = 0;

	m_status.value = 0;
	m_status.readyToReceiveCommand = true;
	m_status.displayDisable = true;
	m_renderer.SetSemiTransparencyMode( m_status.GetSemiTransparencyMode() );
	m_renderer.SetDrawMode( m_status.GetTexPage(), ClutAttribute{ 0 } );
	m_renderer.SetMaskBits( m_status.setMaskOnDraw, m_status.checkMaskOnDraw );
	m_renderer.SetDisplaySize( GetHorizontalResolution(), GetVerticalResolution() );
	m_renderer.SetColorDepth( m_status.GetDisplayAreaColorDepth() );
	m_renderer.SetDisplayEnable( !m_status.displayDisable );

	m_texturedRectFlipX = false;
	m_texturedRectFlipY = false;

	m_textureWindowMaskX = 0;
	m_textureWindowMaskY = 0;
	m_textureWindowOffsetX = 0;
	m_textureWindowOffsetY = 0;
	m_renderer.SetTextureWindow( 0, 0, 0, 0 );

	m_drawAreaLeft = 0;
	m_drawAreaTop = 0;
	m_drawAreaRight = 0;
	m_drawAreaBottom = 0;
	m_renderer.SetDrawArea( 0, 0, 0, 0 );

	m_drawOffsetX = 0;
	m_drawOffsetY = 0;

	m_displayAreaStartX = 0;
	m_displayAreaStartY = 0;
	m_renderer.SetDisplayStart( 0, 0 );

	m_horDisplayRangeStart = 0;
	m_horDisplayRangeEnd = 0;

	m_verDisplayRangeStart = 0;
	m_verDisplayRangeEnd = 0;

	m_currentScanline = 0;
	m_currentDot = 0.0f;
	m_dotTimerFraction = 0.0f;
	m_hblank = false;
	m_vblank = false;
	m_drawingEvenOddLine = false;

	m_displayFrame = false;

	// clear VRAM
	std::fill_n( m_vram.get(), VRamWidth * VRamHeight, uint16_t{ 0 } );
	m_renderer.FillVRam( 0, 0, VRamWidth, VRamHeight, 0, 0, 0, 0 );

	m_vramCopyState.reset();

	m_renderer.SetDisplaySize( GetHorizontalResolution(), GetVerticalResolution() );
	m_renderer.SetColorDepth( static_cast<DisplayAreaColorDepth>( m_status.displayAreaColorDepth ) );

	UpdateDmaRequest();

	ScheduleNextEvent();
}

void Gpu::WriteGP0( uint32_t value ) noexcept
{
	switch ( m_state )
	{
		case State::Idle:
			GP0_Command( value );
			break;

		case State::Parameters:
			GP0_Params( value );
			break;

		case State::WritingVRam:
			GP0_Image( value );
			break;

		case State::ReadingVRam:
			dbBreak();
			break;

		case State::PolyLine:
			GP0_PolyLine( value );
			break;
	}
}

void Gpu::UpdateClockEventEarly()
{
	m_clockEvent->UpdateEarly();
}

void Gpu::ClearCommandBuffer() noexcept
{
	SetState( State::Idle );
	m_commandBuffer.Clear();
	m_remainingParamaters = 0;
	m_commandFunction = nullptr;
}

void Gpu::InitCommand( uint32_t command, uint32_t paramaterCount, CommandFunction function ) noexcept
{
	dbExpects( m_commandBuffer.Empty() );
	dbExpects( paramaterCount > 0 );
	dbExpects( function );
	m_commandBuffer.Push( command );
	m_remainingParamaters = paramaterCount;
	m_commandFunction = function;
	SetState( State::Parameters );
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
	dbExpects( m_state == State::WritingVRam );

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

	m_vramCopyState.reset();
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
			dbLogDebug( "Gpu::GP0_Command() -- set draw mode [%X]", value );
			/*
			0 - 3	Texture page X Base( N * 64 ) ( ie.in 64 - halfword steps ); GPUSTAT.0 - 3
			4		Texture page Y Base( N * 256 ) ( ie. 0 or 256 ); GPUSTAT.4
			5 - 6   Semi Transparency( 0 = B / 2 + F / 2, 1 = B + F, 2 = B - F, 3 = B + F / 4 ); GPUSTAT.5 - 6
			7 - 8   Texture page colors( 0 = 4bit, 1 = 8bit, 2 = 15bit, 3 = Reserved ); GPUSTAT.7 - 8
			9		Dither 24bit to 15bit( 0 = Off / strip LSBs, 1 = Dither Enabled ); GPUSTAT.9
			10		Drawing to display area( 0 = Prohibited, 1 = Allowed ); GPUSTAT.10
			11		Texture Disable( 0 = Normal, 1 = Disable if GP1( 09h ).Bit0 = 1 ); GPUSTAT.15
					( Above might be chipselect for ( absent ) second VRAM chip ? )
			12		Textured Rectangle X - Flip( BIOS does set this bit on power - up... ? )
			13		Textured Rectangle Y - Flip( BIOS does set it equal to GPUSTAT.13... ? )
			14 - 23 Not used( should be 0 )
			*/
			stdx::masked_set<uint32_t>( m_status.value, 0x7ff, value );

			m_status.textureDisable = stdx::any_of<uint32_t>( value, 1 << 11 );

			m_texturedRectFlipX = stdx::any_of<uint32_t>( value, 1 << 12 );
			m_texturedRectFlipY = stdx::any_of<uint32_t>( value, 1 << 13 );
			break;
		}

		case 0xe2: // texture window setting
		{
			dbLogDebug( "Gpu::GP0_Command() -- set texture window [%X]", value );

			m_textureWindowMaskX = value & 0x1f;
			m_textureWindowMaskY = ( value >> 5 ) & 0x1f;
			m_textureWindowOffsetX = ( value >> 10 ) & 0x1f;
			m_textureWindowOffsetY = ( value >> 15 ) & 0x1f;

			m_renderer.SetTextureWindow( m_textureWindowMaskX, m_textureWindowMaskY, m_textureWindowOffsetX, m_textureWindowOffsetY );
			break;
		}

		case 0xe3: // set draw area top-left
		{
			m_drawAreaLeft = value & 0x3ff;
			m_drawAreaTop = ( value >> 10 ) & 0x1ff;

			dbLogDebug( "Gpu::GP0_Command() -- set draw area top-left [%u, %u]", m_drawAreaLeft, m_drawAreaTop );

			m_renderer.SetDrawArea( m_drawAreaLeft, m_drawAreaTop, m_drawAreaRight, m_drawAreaBottom );

			// TODO: does this affect blanking?
			break;
		}

		case 0xe4: // set draw area bottom-right
		{
			m_drawAreaRight = value & 0x3ff;
			m_drawAreaBottom = ( value >> 10 ) & 0x1ff;

			dbLogDebug( "Gpu::GP0_Command() -- set draw area bottom-right [%u, %u]", m_drawAreaRight, m_drawAreaBottom );

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
			dbLogDebug( "Gpu::GP0_Command() -- set draw offset [%u, %u]", m_drawOffsetX, m_drawOffsetY );
			break;
		}

		case 0xe6: // mask bit setting
		{
			const bool setMask = value & 0x01;
			const bool checkMask = value & 0x02;
			dbLogDebug( "Gpu::GP0_Command() -- set mask bits [set:%i check:%i]", setMask, checkMask );

			m_status.setMaskOnDraw = setMask;
			m_status.checkMaskOnDraw = checkMask;
			m_renderer.SetMaskBits( setMask, checkMask );
			break;
		}

		case 0x01: // clear cache
			dbLogDebug( "Gpu::GP0_Command() -- clear GPU cache" );
			break;

		case 0x02: // fill rectangle in VRAM
			dbLogDebug( "Gpu::GP0_Command() -- fill rectangle in VRAM" );
			InitCommand( value, 2, &Gpu::FillRectangle );
			break;

		case 0x80: // copy rectangle (VRAM to VRAM)
			dbLogDebug( "Gpu::GP0_Command() -- copy rectangle (VRAM to VRAM)" );
			InitCommand( value, 3, &Gpu::CopyRectangle );
			break;

		case 0xa0: // copy rectangle (CPU to VRAM)
			dbLogDebug( "Gpu::GP0_Command() -- copy rectangle (CPU to VRAM)" );
			InitCommand( value, 2, &Gpu::CopyRectangleToVram );
			break;

		case 0xc0: // copy rectangle (VRAM to CPU)
			dbLogDebug( "Gpu::GP0_Command() -- copy rectangle (VRAM to CPU)" );
			InitCommand( value, 2, &Gpu::CopyRectangleFromVram );
			break;

		case 0x1f: // interrupt request
		{
			dbLogDebug( "Gpu::GP0_Command() -- request interrupt" );
			if ( !m_status.interruptRequest ) // edge triggered
			{
				m_status.interruptRequest = true;
				m_interruptControl.SetInterrupt( Interrupt::Gpu );
			}
			break;
		}

		case 0x03: // unknown. Takes up space in FIFO
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
			const RenderCommand command{ value };
			switch ( static_cast<PrimitiveType>( command.type ) )
			{
				case PrimitiveType::Polygon:
				{
					const uint32_t params = ( command.quadPolygon ? 4 : 3 ) * ( 1 + command.textureMapping + command.shading ) - command.shading;
					InitCommand( value, params, &Gpu::RenderPolygon );
					break;
				}

				case PrimitiveType::Line:
				{
					m_commandBuffer.Push( value );
					m_commandFunction = &Gpu::TempFinishCommandParams;
					m_remainingParamaters = command.shading ? 3 : 2;
					SetState( command.numLines ? State::PolyLine : State::Parameters );
					break;
				}

				case PrimitiveType::Rectangle:
				{
					const uint32_t params = 1 + ( command.rectSize == 0 ) + command.textureMapping;
					InitCommand( value, params, &Gpu::RenderRectangle );
					break;
				}

				default:
				{
					dbLogWarning( "Gpu::GP0_Command() -- invalid GP0 opcode [%X]", opcode );
					ClearCommandBuffer();
					break;
				}
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
	static constexpr uint32_t TerminationMask = 0xf000f000; // duckstation masks the param before checking against the termination code
	static constexpr uint32_t TerminationCode = 0x50005000; // supposedly 0x55555555, but Wild Arms 2 uses 0x50005000

	// always read the first few parameters
	if ( m_remainingParamaters == 0 )
	{
		if ( ( param & TerminationMask ) == TerminationCode )
		{
			std::invoke( m_commandFunction, this );
			return;
		}
	}
	else
	{
		--m_remainingParamaters;
	}

	m_commandBuffer.Push( param );
}

void Gpu::GP0_Image( uint32_t param ) noexcept
{
	dbExpects( m_vramCopyState.has_value() );
	dbExpects( !m_vramCopyState->IsFinished() );

	m_vramCopyState->PushPixel( static_cast<uint16_t>( param ) );

	if ( !m_vramCopyState->IsFinished() )
		m_vramCopyState->PushPixel( static_cast<uint16_t>( param >> 16 ) );

	if ( m_vramCopyState->IsFinished() )
	{
		dbLogDebug( "Gpu::GP0_Image -- transfer finished" );
		FinishVRamTransfer();
		ClearCommandBuffer();
		UpdateDmaRequest();
	}
}

uint32_t Gpu::GpuRead() noexcept
{
	if ( m_state != State::ReadingVRam )
		return m_gpuRead;

	dbExpects( m_vramCopyState.has_value() );
	dbExpects( !m_vramCopyState->IsFinished() );

	auto getPixel = [this]() -> uint32_t
	{
		const auto x = m_vramCopyState->GetWrappedX();
		const auto y = m_vramCopyState->GetWrappedY();
		m_vramCopyState->Increment();
		return m_vram[ x + y * VRamWidth ];
	};

	uint32_t result = getPixel();

	if ( !m_vramCopyState->IsFinished() )
		result |= getPixel() << 16;

	if ( m_vramCopyState->IsFinished() )
	{
		dbLogDebug( "Gpu::GpuRead_Image -- finished transfer" );
		SetState( State::Idle );
		m_vramCopyState.reset();
		UpdateDmaRequest();
	}

	m_gpuRead = result;
	return result;
}

void Gpu::WriteGP1( uint32_t value ) noexcept
{
	const uint8_t opcode = static_cast<uint8_t>( value >> 24 );

	switch ( opcode & 0x3f ) // opcodes mirror 0x00-0x3f
	{
		case 0x00: // soft reset GPU
		{
			dbLogDebug( "Gpu::WriteGP1() -- soft reset" );

			ClearCommandBuffer();

			m_status.value = 0x14802000;
			m_renderer.SetSemiTransparencyMode( m_status.GetSemiTransparencyMode() );
			m_renderer.SetDrawMode( m_status.GetTexPage(), ClutAttribute{ 0 } );
			m_renderer.SetMaskBits( m_status.setMaskOnDraw, m_status.checkMaskOnDraw );
			m_renderer.SetDisplaySize( GetHorizontalResolution(), GetVerticalResolution() );
			m_renderer.SetColorDepth( m_status.GetDisplayAreaColorDepth() );
			m_renderer.SetDisplayEnable( !m_status.displayDisable );

			m_horDisplayRangeStart = 0x200;
			m_horDisplayRangeEnd = 0x200 + 256 * 10;
			m_verDisplayRangeStart = 0x10;
			m_verDisplayRangeEnd = 0x10 + 240;

			m_texturedRectFlipX = false;
			m_texturedRectFlipY = false;

			m_textureWindowMaskX = 0;
			m_textureWindowMaskY = 0;
			m_textureWindowOffsetX = 0;
			m_textureWindowOffsetY = 0;
			m_renderer.SetTextureWindow( 0, 0, 0, 0 );

			// TODO: does this affect hblanking?
			m_drawAreaLeft = 0;
			m_drawAreaTop = 0;
			m_drawAreaRight = 0;
			m_drawAreaBottom = 0;
			m_renderer.SetDrawArea( 0, 0, 0, 0 );

			m_drawOffsetX = 0;
			m_drawOffsetY = 0;

			m_currentScanline = 0;
			m_currentDot = 0;
			m_dotTimerFraction = 0;
			m_hblank = false;
			m_vblank = false;
			m_drawingEvenOddLine = false;

			auto& dotTimer = m_timers->GetTimer( 0 );
			dotTimer.UpdateBlank( m_hblank );

			auto& hblankTimer = m_timers->GetTimer( 1 );
			hblankTimer.UpdateBlank( m_vblank );

			UpdateDmaRequest();

			ScheduleNextEvent();

			// TODO: clear texture cache?

			break;
		}

		case 0x01: // reset command buffer
			dbLogDebug( "Gpu::WriteGP1() -- clear command buffer" );
			ClearCommandBuffer();
			UpdateDmaRequest();
			break;

		case 0x02: // ack GPU interrupt
			dbLogDebug( "Gpu::WriteGP1() -- acknowledge interrupt" );
			m_status.interruptRequest = false;
			break;

		case 0x03: // display enable
		{
			const bool disableDisplay = value & 0x1;
			dbLogDebug( "Gpu::WriteGP1() -- enable display: %s", disableDisplay ? "false" : "true" );
			m_status.displayDisable = disableDisplay;
			m_renderer.SetDisplayEnable( !disableDisplay );
			break;
		}

		case 0x04: // DMA direction / data request
		{
			uint32_t newDirection = value & 0x3;
			dbLogDebug( "Gpu::WriteGP1() -- set DMA direction: %u", newDirection );

			if ( m_status.dmaDirection != newDirection )
			{
				m_status.dmaDirection = newDirection;
				UpdateDmaRequest();
			}
			break;
		}

		case 0x05: // start of display area
		{
			m_displayAreaStartX = value & 0x3fe;
			m_displayAreaStartY = ( value >> 10 ) & 0x1ff;
			dbLogDebug( "Gpu::WriteGP1() -- set display area start [%u, %u]", m_displayAreaStartX, m_displayAreaStartY );
			m_renderer.SetDisplayStart( m_displayAreaStartX, m_displayAreaStartY );
			break;
		}

		case 0x06: // horizontal display range
		{
			m_horDisplayRangeStart = value & 0xfff;
			m_horDisplayRangeEnd = ( value >> 12 ) & 0xfff;
			dbLogDebug( "Gpu::WriteGP1() -- set horizontal display range [%u, %u]", m_horDisplayRangeStart, m_horDisplayRangeEnd );
			break;
		}

		case 0x07: // vertical display range
		{
			m_verDisplayRangeStart = value & 0x3ff;
			m_verDisplayRangeEnd = ( value >> 10 ) & 0x3ff;
			dbLogDebug( "Gpu::WriteGP1() -- set vertical display range [%u]", m_verDisplayRangeEnd - m_verDisplayRangeStart );
			break;
		}

		case 0x08: // display mode
		{
			// set resolution, video mode, color depth, interlacing, reverse flag

			dbLogDebug( "Gpu::WriteGP1() -- set display mode [%X]", value );

			Status newStatus = m_status;

			// bits 0-5 same as GPUSTAT bits 17-22
			stdx::masked_set<uint32_t>( newStatus.value, 0x3f << 17, value << 17 );
			newStatus.horizontalResolution2 = ( value >> 6 ) & 1;
			newStatus.reverseFlag = ( value >> 7 ) & 1;

			// update cycles and renderer if the new status is different
			if ( newStatus.value != m_status.value )
			{
				m_clockEvent->UpdateEarly();

				m_status.value = newStatus.value;
				m_renderer.SetDisplaySize( GetHorizontalResolution(), GetVerticalResolution() );
				m_renderer.SetColorDepth( static_cast<DisplayAreaColorDepth>( m_status.displayAreaColorDepth ) );

				ScheduleNextEvent();
			}
			break;
		}

		case 0x09: // new texture disable
		{
			dbLogDebug( "Gpu::WriteGP1() -- set texture disable [%X]", value );
			m_status.textureDisable = value & 0x01;
			break;
		}

		case 0x10: case 0x11: case 0x12: case 0x13:
		case 0x14: case 0x15: case 0x16: case 0x17:
		case 0x18: case 0x19: case 0x1a: case 0x1b:
		case 0x1c: case 0x1d: case 0x1e: case 0x1f:
		{
			// get GPU info

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
			dbLogWarning( "unhandled GP1 opcode [%x]", opcode );
			break;
	}
}

uint32_t Gpu::GpuStatus() noexcept
{
	// update timers if it could affect the even/odd bit
	const auto dotAfterCycles = m_currentDot + m_clockEvent->GetPendingCycles() * GetDotsPerVideoCycle();
	if ( dotAfterCycles >= GetDotsPerScanline() )
	{
		m_clockEvent->UpdateEarly();
	}

	return m_status.value;
}

void Gpu::UpdateDmaRequest() noexcept
{
	switch ( m_state )
	{
		case State::Idle:
		case State::Parameters:
		case State::PolyLine:
			m_status.readyToReceiveDmaBlock = m_commandBuffer.Empty() || ( m_remainingParamaters > 0 );
			m_status.readyToSendVRamToCpu = false;
			break;

		case State::WritingVRam:
			m_status.readyToReceiveDmaBlock = true;
			m_status.readyToSendVRamToCpu = false;
			break;

		case State::ReadingVRam:
			m_status.readyToReceiveDmaBlock = false;
			m_status.readyToSendVRamToCpu = true;
			break;
	}

	bool dmaRequest = false;
	switch ( static_cast<DmaDirection>( m_status.dmaDirection ) )
	{
		case DmaDirection::Off:
			dmaRequest = false;
			break;

		case DmaDirection::Fifo:
			dmaRequest = !m_commandBuffer.Empty();
			break;

		case DmaDirection::CpuToGp0:
			dmaRequest = m_status.readyToReceiveDmaBlock;
			break;

		case DmaDirection::GpuReadToCpu:
			dmaRequest = m_status.readyToSendVRamToCpu;
			break;
	}

	m_dma->SetRequest( Dma::Channel::Gpu, dmaRequest );
}

void Gpu::FillRectangle() noexcept
{
	// not affected by mask settings

	const Color color{ m_commandBuffer.Pop() };
	auto[ x, y ] = DecodeFillPosition( m_commandBuffer.Pop() );
	auto[ width, height ] = DecodeFillSize( m_commandBuffer.Pop() );

	dbLogDebug( "Gpu::FillRectangle() -- pos: %u,%u size: %u,%u", x, y, width, height );

	if ( width > 0 && height > 0 )
	{
		const float r = static_cast<float>( color.r ) / 255.0f;
		const float g = static_cast<float>( color.g ) / 255.0f;
		const float b = static_cast<float>( color.b ) / 255.0f;
		m_renderer.FillVRam( x, y, width, height, r, g, b, 0.0f );
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

	dbLogDebug( "Gpu::CopyRectangle() -- srcPos: %u,%u destPos: %u,%u size: %u,%u", srcX, srcY, destX, destY, width, height );

	m_renderer.CopyVRam( srcX, srcY, destX, destY, width, height );

	ClearCommandBuffer();
}

void Gpu::CopyRectangleToVram() noexcept
{
	// affected by mask settings
	SetupVRamCopy();
	dbLogDebug( "Gpu::CopyRectangleToVram() -- pos: %u,%u size: %u,%u", m_vramCopyState->left, m_vramCopyState->top, m_vramCopyState->width, m_vramCopyState->height );

	m_vramCopyState->InitializePixelBuffer();
	
	SetState( State::WritingVRam );
	UpdateDmaRequest();
}

void Gpu::CopyRectangleFromVram() noexcept
{
	SetupVRamCopy();
	dbLogDebug( "Gpu::CopyRectangleFromVram() -- pos: %u,%u size: %u,%u", m_vramCopyState->left, m_vramCopyState->top, m_vramCopyState->width, m_vramCopyState->height );
	ClearCommandBuffer(); // TODO: clear buffer here after image copy?

	SetState( State::ReadingVRam );

	m_renderer.ReadVRam( m_vramCopyState->left, m_vramCopyState->top, m_vramCopyState->width, m_vramCopyState->height, m_vram.get() );

	UpdateDmaRequest();
}

void Gpu::RenderPolygon() noexcept
{
	Vertex vertices[ 4 ];

	const RenderCommand command = m_commandBuffer.Pop();

	// vertex 1

	if ( command.shading )
	{
		vertices[ 0 ].color = Color{ command.color };
	}
	else
	{
		const bool noBlend = command.textureMode && command.textureMapping;
		const Color color{ noBlend ? 0x808080 : command.color };
		for ( auto& v : vertices )
			v.color = color;
	}

	vertices[ 0 ].position = PositionParameter{ m_commandBuffer.Pop() };

	TexPage texPage;
	ClutAttribute clut;
	if ( command.textureMapping )
	{
		const auto value = m_commandBuffer.Pop();
		vertices[ 0 ].texCoord = TexCoord{ value };

		clut = static_cast<uint16_t>( value >> 16 );
		for ( auto& v : vertices )
			v.clut = clut;
	}

	// vertex 2

	if ( command.shading )
		vertices[ 1 ].color = Color{ m_commandBuffer.Pop() };

	vertices[ 1 ].position = PositionParameter{ m_commandBuffer.Pop() };

	if ( command.textureMapping )
	{
		const auto value = m_commandBuffer.Pop();
		vertices[ 1 ].texCoord = TexCoord{ value };
		texPage = static_cast<uint16_t>( value >> 16 );
	}
	else
	{
		texPage = m_status.GetTexPage();
		texPage.textureDisable = true;
	}
	for ( auto& v : vertices )
		v.texPage = texPage;

	// vetex 3 and 4

	const size_t numVertices = command.quadPolygon ? 4 : 3;

	for ( size_t i = 2; i < numVertices; ++i )
	{
		if ( command.shading )
			vertices[ i ].color = Color{ m_commandBuffer.Pop() };

		vertices[ i ].position = PositionParameter{ m_commandBuffer.Pop() };

		if ( command.textureMapping )
			vertices[ i ].texCoord = TexCoord{ m_commandBuffer.Pop() };
	}

	for ( size_t i = 0; i < numVertices; ++i )
	{
		vertices[ i ].position.x += m_drawOffsetX;
		vertices[ i ].position.y += m_drawOffsetY;
	}

	// TODO: check for large polygons

	m_renderer.SetDrawMode( texPage, clut );

#if GPU_RENDER_POLYGONS
	m_renderer.PushTriangle( vertices, command.semiTransparency );
	if ( command.quadPolygon )
		m_renderer.PushTriangle( vertices + 1, command.semiTransparency );
#endif

	ClearCommandBuffer();
}

void Gpu::RenderRectangle() noexcept
{
	// FlushVRam();

	Vertex vertices[ 4 ];

	const RenderCommand command = m_commandBuffer.Pop();

	// set color
	const bool noBlend = command.textureMode && command.textureMapping;
	const Color color{ noBlend ? 0x808080 : command.color };
	for ( auto& v : vertices )
		v.color = color;

	// get position
	const Position pos = Position( PositionParameter( m_commandBuffer.Pop() ) ) + Position( m_drawOffsetX, m_drawOffsetY );

	// get tex coord/set clut
	TexCoord topLeftTexCoord;

	TexPage texPage = m_status.GetTexPage();
	ClutAttribute clut;

	if ( command.textureMapping )
	{
		const uint32_t value = m_commandBuffer.Pop();

		topLeftTexCoord = TexCoord{ value };

		clut = static_cast<uint16_t>( value >> 16 );
		for ( auto& v : vertices )
		{
			v.clut = clut;
			v.texPage = texPage;
		}
	}
	else
	{
		texPage.textureDisable = true;
		for ( auto& v : vertices )
			v.texPage = texPage;
	}

	int16_t width = 0;
	int16_t height = 0;
	switch ( static_cast<RectangleSize>( command.rectSize ) )
	{
		case RectangleSize::Variable:
		{
			const uint32_t sizeParam = m_commandBuffer.Pop();
			width = static_cast<int16_t>( sizeParam & VRamWidthMask );
			height = static_cast<int16_t>( ( sizeParam >> 16 ) & VRamHeightMask );

			if ( width == 0 || height == 0 )
			{
				ClearCommandBuffer();
				return;
			}
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
	}

	vertices[ 0 ].position = pos;
	vertices[ 1 ].position = Position{ pos.x,									static_cast<int16_t>( pos.y + height ) };
	vertices[ 2 ].position = Position{ static_cast<int16_t>( pos.x + width ),	pos.y };
	vertices[ 3 ].position = Position{ static_cast<int16_t>( pos.x + width ),	static_cast<int16_t>( pos.y + height ) };

	if ( command.textureMapping )
	{
		vertices[ 0 ].texCoord = topLeftTexCoord;
		vertices[ 1 ].texCoord = TexCoord{ topLeftTexCoord.u,										static_cast<uint16_t>( topLeftTexCoord.v + height - 1 ) };
		vertices[ 2 ].texCoord = TexCoord{ static_cast<uint16_t>( topLeftTexCoord.u + width - 1 ),	topLeftTexCoord.v };
		vertices[ 3 ].texCoord = TexCoord{ static_cast<uint16_t>( topLeftTexCoord.u + width - 1 ),	static_cast<uint16_t>( topLeftTexCoord.v + height - 1 ) };
	}

	m_renderer.SetDrawMode( texPage, clut );

#if GPU_RENDER_RECTANGLES
	m_renderer.PushQuad( vertices, command.semiTransparency );
#endif

	ClearCommandBuffer();
}

void Gpu::UpdateCycles( float gpuTicks ) noexcept
{
	const float dots = gpuTicks * GetDotsPerVideoCycle();

	auto& dotTimer = m_timers->GetTimer( 0 );
	if ( !dotTimer.IsUsingSystemClock() )
	{
		m_dotTimerFraction += dots;
		dotTimer.Update( static_cast<uint32_t>( m_dotTimerFraction ) );
		m_dotTimerFraction = std::fmod( m_dotTimerFraction, 1.0f );
	}

	// update render position
	const auto scanlineCount = GetScanlines();
	const auto dotsPerScanline = GetDotsPerScanline();
	m_currentDot += dots;
	while ( m_currentDot >= dotsPerScanline )
	{
		m_currentDot -= dotsPerScanline;
		m_currentScanline = ( m_currentScanline + 1 ) % scanlineCount;

		if ( !IsInterlaced() || m_currentScanline == 0 )
			m_drawingEvenOddLine ^= 1;
	}

	// check for hblank
	const bool hblank = m_currentDot >= GetHorizontalResolution();
	dotTimer.UpdateBlank( hblank );

	auto& hblankTimer = m_timers->GetTimer( 1 );
	if ( !hblankTimer.IsUsingSystemClock() )
	{
		const uint32_t lines = static_cast<uint32_t>( dots / dotsPerScanline );
		hblankTimer.Update( static_cast<uint32_t>( hblank && !m_hblank ) + lines );
	}

	m_hblank = hblank;

	// check for vblank

	const bool vblank = m_currentScanline < m_verDisplayRangeStart || m_currentScanline >= m_verDisplayRangeEnd;

	if ( vblank != m_vblank )
	{
		m_vblank = vblank;
		hblankTimer.UpdateBlank( vblank );

		if ( vblank )
		{
			dbLogDebug( "VBlank start" );
			m_interruptControl.SetInterrupt( Interrupt::VBlank );
			m_displayFrame = true;
		}
		else
		{
			dbLogDebug( "VBlank end" );
		}
	}

	// In 480-lines mode, bit31 changes per frame. And in 240-lines mode, the bit changes per scanline.
	// The bit is always zero during Vblank (vertical retrace and upper/lower screen border).
	m_status.evenOddVblank = m_drawingEvenOddLine && !m_vblank;

	ScheduleNextEvent();
}

void Gpu::ScheduleNextEvent()
{
	float gpuTicks = std::numeric_limits<float>::max();

	const auto horRez = GetHorizontalResolution();

	const float dotsPerCycle = GetDotsPerVideoCycle();

	// timer0
	{
		auto& dotTimer = m_timers->GetTimer( 0 );

		if ( dotTimer.GetSyncEnable() ) // dot timer synchronizes with hblanks
		{
			const float ticksUntilHblankChange = ( ( m_currentDot < horRez ? horRez : GetDotsPerScanline() ) - m_currentDot ) / dotsPerCycle;

			gpuTicks = std::min( gpuTicks, ticksUntilHblankChange );
		}

		if ( !dotTimer.IsUsingSystemClock() && !dotTimer.IsPaused() )
		{
			const float ticksUntilIrq = dotTimer.GetTicksUntilIrq() / dotsPerCycle;

			gpuTicks = std::min( gpuTicks, ticksUntilIrq );
		}
	}

	const uint32_t linesUntilVblankChange = ( m_currentScanline < m_verDisplayRangeStart )
		? ( m_verDisplayRangeStart - m_currentScanline )
		: ( m_currentScanline < m_verDisplayRangeEnd )
		? ( m_verDisplayRangeEnd - m_currentScanline )
		: ( GetScanlines() - m_currentScanline + m_verDisplayRangeStart );

	const float ticksUntilVblankChange = linesUntilVblankChange * GetVideoCyclesPerScanline() - m_currentDot / dotsPerCycle;
	gpuTicks = std::min( gpuTicks, ticksUntilVblankChange );

	// timer1
	{
		auto& scanlineTimer = m_timers->GetTimer( 1 );
		if ( !scanlineTimer.IsUsingSystemClock() && !scanlineTimer.IsPaused() )
		{
			const float ticksUntilHblank = ( ( m_currentDot < horRez ? horRez : GetDotsPerScanline() + horRez ) - m_currentDot ) / dotsPerCycle;
			const float ticksUntilIrq = scanlineTimer.GetTicksUntilIrq() * GetVideoCyclesPerScanline() - ticksUntilHblank;

			gpuTicks = std::min( gpuTicks, ticksUntilIrq );
		}
	}

	const auto cpuCycles = static_cast<cycles_t>( std::ceil( ConvertVideoToCpuCycles( gpuTicks ) ) );

	m_cachedCyclesUntilNextEvent = cpuCycles;

	m_clockEvent->Schedule( cpuCycles );
}

} // namespace PSX