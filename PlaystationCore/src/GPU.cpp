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

template <typename T>
inline constexpr T UnitsUntilRangeChange( T current, T start, T end, T wrappingSize )
{
	return ( current < start ) ? ( start - current ) : ( current < end ) ? ( end - current ) : ( wrappingSize - current + start );
};

template <typename T>
inline constexpr T UnitsUntilTrigger( T current, T trigger, T wrappingSize )
{
	return ( current < trigger ) ? ( trigger - current ) : ( wrappingSize - current + trigger );
};

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

} // namespace

Gpu::Gpu( InterruptControl& interruptControl, Renderer& renderer, EventManager& eventManager )
	: m_interruptControl{ interruptControl }
	, m_renderer{ renderer }
	, m_vram{ std::make_unique<uint16_t[]>( VRamWidth * VRamHeight ) } // 1MB of VRAM
{
	m_crtEvent = eventManager.CreateEvent( "GPU clock event", [this]( cycles_t cpuCycles )
		{
			UpdateCrtCycles( cpuCycles );
		} );

	m_commandEvent = eventManager.CreateEvent( "GPU command event", [this]( cycles_t cpuCycles )
		{
			UpdateCommandCycles( cpuCycles );
		} );
}

Gpu::~Gpu() = default;


void Gpu::ClearCommandBuffer() noexcept
{
	if ( m_state == State::WritingVRam )
		FinishVRamWrite();

	m_state = State::Idle;
	m_commandBuffer.Clear();
	m_remainingParamaters = 0;
	m_commandFunction = nullptr;

	m_transferBuffer.clear();
	m_vramTransferState.reset();
}

void Gpu::SoftReset() noexcept
{
	ClearCommandBuffer();

	// reset GPUSTAT
	m_status.value = 0x14802000;
	m_renderer.SetSemiTransparencyMode( m_status.GetSemiTransparencyMode() );
	m_renderer.SetDrawMode( m_status.GetTexPage(), ClutAttribute{ 0 }, false );
	m_renderer.SetMaskBits( m_status.setMaskOnDraw, m_status.checkMaskOnDraw );
	m_renderer.SetDisplaySize( GetHorizontalResolution(), GetVerticalResolution() );
	m_renderer.SetColorDepth( m_status.GetDisplayAreaColorDepth() );
	m_renderer.SetDisplayEnable( !m_status.displayDisable );
	
	// reset texture rect flip
	m_texturedRectFlipX = false;
	m_texturedRectFlipY = false;

	// reset texture window
	m_textureWindowMaskX = 0;
	m_textureWindowMaskY = 0;
	m_textureWindowOffsetX = 0;
	m_textureWindowOffsetY = 0;
	m_renderer.SetTextureWindow( 0, 0, 0, 0 );

	// reset draw area
	m_drawAreaLeft = 0;
	m_drawAreaTop = 0;
	m_drawAreaRight = 0;
	m_drawAreaBottom = 0;
	m_renderer.SetDrawArea( 0, 0, 0, 0 );

	// reset draw offset
	m_drawOffsetX = 0;
	m_drawOffsetY = 0;

	// reset display address
	m_displayAreaStartX = 0;
	m_displayAreaStartY = 0;
	m_renderer.SetDisplayStart( 0, 0 );

	// reset horizontal display range
	m_horDisplayRangeStart = 0x260;
	m_horDisplayRangeEnd = 0x260 + 320 * 8;

	// reset vertical display range
	m_verDisplayRangeStart = 0x88 - 224 / 2;
	m_verDisplayRangeEnd = 0x88 + 224 / 2;

	UpdateCrtConstants();
	ScheduleCrtEvent();
	UpdateDmaRequest();
}

void Gpu::Reset()
{
	m_crtEvent->Reset();
	m_commandEvent->Reset();

	m_pendingCommandCycles = 0;
	m_processingCommandBuffer = false;

	m_gpuRead = 0;

	m_crtState = CrtState{};
	m_cachedCyclesUntilNextEvent = 0;

	// clear VRAM
	std::fill_n( m_vram.get(), VRamWidth * VRamHeight, uint16_t{ 0 } );
	m_renderer.FillVRam( 0, 0, VRamWidth, VRamHeight, 0, 0, 0, 0 );

	// reset buffers
	m_commandBuffer.Reset();
	m_transferBuffer.clear();
	m_transferBuffer.shrink_to_fit();

	SoftReset();
}

double Gpu::GetRefreshRate() const noexcept
{
	constexpr double GpuCyclesPerSecond = ConvertCpuToGpuCycles( CpuCyclesPerSecond );
	const double gpuCyclesPerframe = static_cast<double>( m_crtConstants.totalScanlines ) * static_cast<double>( m_crtConstants.cyclesPerScanline );
	return GpuCyclesPerSecond / gpuCyclesPerframe;
}

void Gpu::WriteGP0( uint32_t value ) noexcept
{
	if ( m_commandBuffer.Full() )
	{
		dbBreakMessage( "Gpu::WriteGP0 -- command buffer is full" );
		return;
	}

	m_commandBuffer.Push( value );
	ProcessCommandBuffer();
}


void Gpu::ProcessCommandBuffer() noexcept
{
	m_processingCommandBuffer = true;

	const auto oldPendingCommandCycles = m_pendingCommandCycles;

	for ( ;; )
	{
		if ( !m_commandBuffer.Empty() && m_pendingCommandCycles <= MaxRunAheadCommandCycles )
		{
			switch ( m_state )
			{
				case State::Idle:
				{
					ExecuteCommand();

					if ( m_state != State::Parameters )
						continue;
				}

				[[fallthrough]];
				case State::Parameters:
				{
					if ( m_commandBuffer.Size() >= m_remainingParamaters + 1 ) // +1 for command
					{
						std::invoke( m_commandFunction, this );
						continue;
					}

					// need more parameters, request DMA
					break;
				}

				case State::WritingVRam:
				{
					dbAssert( m_vramTransferState.has_value() );
					dbAssert( !m_vramTransferState->IsFinished() );

					const uint32_t available = std::min( m_remainingParamaters, m_commandBuffer.Size() );
					for ( uint32_t i = 0; i < available; ++i )
						m_transferBuffer.push_back( m_commandBuffer.Pop() );

					m_remainingParamaters -= available;
					if ( m_remainingParamaters == 0 )
					{
						dbLogDebug( "Gpu::GP0_Image -- transfer finished" );
						FinishVRamWrite();
						continue;
					}

					// need more data, request DMA
					break;
				}

				case State::ReadingVRam:
					// nothing to do while reading VRAM
					break;

				case State::PolyLine:
				{
					while ( m_remainingParamaters > 0 && !m_commandBuffer.Empty() )
					{
						--m_remainingParamaters;
						m_transferBuffer.push_back( m_commandBuffer.Pop() );
					}

					static constexpr uint32_t TerminationMask = 0xf000f000; // duckstation masks the param before checking against the termination code
					static constexpr uint32_t TerminationCode = 0x50005000; // supposedly 0x55555555, but Wild Arms 2 uses 0x50005000

					const uint32_t paramsPerVertex = RenderCommand{ m_transferBuffer.front() }.shading ? 2 : 1;
					uint32_t paramIndex = m_transferBuffer.size();

					while ( !m_commandBuffer.Empty() )
					{
						const uint32_t param = m_commandBuffer.Pop();
						if ( ( paramIndex % paramsPerVertex == 0 ) && ( ( param & TerminationMask ) == TerminationCode ) )
						{
							Command_RenderPolyLine();
							continue;
						}
						m_transferBuffer.push_back( param );
					}

					// need more parameters, request DMA
					break;
				}
			}
		}

		// try to request more data
		const auto sizeBefore = m_commandBuffer.Size();
		UpdateDmaRequest();

		// stop processing if we didn't get any new data
		if ( sizeBefore == m_commandBuffer.Size() )
			break;
	}

	// schedule end of command execution
	if ( m_pendingCommandCycles > oldPendingCommandCycles )
		m_commandEvent->Schedule( ConvertCommandToCpuCycles( m_pendingCommandCycles ) );

	m_processingCommandBuffer = false;
}

void Gpu::UpdateCommandCycles( cycles_t cpuCycles ) noexcept
{
	m_pendingCommandCycles -= ConvertCpuToCommandCycles( cpuCycles );
	if ( m_pendingCommandCycles <= 0 )
	{
		m_pendingCommandCycles = 0;

		if ( !m_processingCommandBuffer )
			ProcessCommandBuffer();
		else
			UpdateDmaRequest();
	}
}

void Gpu::DmaIn( const uint32_t* input, uint32_t count ) noexcept
{
	if ( m_status.GetDmaDirection() != DmaDirection::CpuToGp0 )
	{
		dbLogWarning( "Gpu::DmaIn -- DMA direction not set to 'CPU -> GP0'" );
		return;
	}

	if ( count > m_commandBuffer.Capacity() )
		dbBreakMessage( "GPU::DmaIn -- command buffer overrun" );

	count = std::min( count, m_commandBuffer.Capacity() );
	m_commandBuffer.Push( input, count );

	// prevent recursive calls
	if ( !m_processingCommandBuffer )
		ProcessCommandBuffer();
	else
		UpdateDmaRequest();
}

void Gpu::DmaOut( uint32_t* output, uint32_t count ) noexcept
{
	if ( m_status.GetDmaDirection() != DmaDirection::GpuReadToCpu )
	{
		dbLogWarning( "Gpu::DmaOut -- DMA direction not set to 'GPUREAD -> CPU'" );
		std::fill_n( output, count, uint32_t( 0xffffffff ) );
		return;
	}

	for ( uint32_t i = 0; i < count; ++i )
		output[ i ] = GpuRead();
}

void Gpu::UpdateCrtEventEarly()
{
	m_crtEvent->UpdateEarly();
}

void Gpu::InitCommand( uint32_t paramaterCount, CommandFunction function ) noexcept
{
	dbExpects( m_state == State::Idle );
	dbExpects( paramaterCount > 0 );
	dbExpects( function );

	m_remainingParamaters = paramaterCount;
	m_commandFunction = function;
	m_state = State::Parameters;
}

void Gpu::SetupVRamCopy() noexcept
{
	dbExpects( !m_vramTransferState.has_value() ); // already doing a copy!

	m_commandBuffer.Pop(); // pop command

	auto& state = m_vramTransferState.emplace();
	std::tie( state.left, state.top ) = DecodeCopyPosition( m_commandBuffer.Pop() );
	std::tie( state.width, state.height ) = DecodeCopySize( m_commandBuffer.Pop() );
}

void Gpu::FinishVRamWrite() noexcept
{
	dbExpects( m_state == State::WritingVRam );
	dbExpects( m_vramTransferState.has_value() );
	dbExpects( !m_transferBuffer.empty() );

	// pixel transfer may be incomplete

	auto& state = *m_vramTransferState;
	const uint16_t* pixels = reinterpret_cast<const uint16_t*>( m_transferBuffer.data() );

	if ( m_remainingParamaters == 0 )
	{
		m_renderer.UpdateVRam(
			state.left, state.top,
			state.width, state.height,
			pixels );
	}
	else
	{
		const uint32_t pixelCount = m_transferBuffer.size() * 2;
		const uint32_t fullLines = pixelCount / state.width;
		const uint32_t lastLineWidth = pixelCount % state.width;

		if ( fullLines > 0 )
		{
			m_renderer.UpdateVRam(
				state.left, state.top,
				state.width, fullLines,
				pixels );
		}

		if ( lastLineWidth > 0 )
		{
			const uint32_t top = state.top + fullLines;
			const size_t bufferOffset = lastLineWidth + fullLines * state.width;
			m_renderer.UpdateVRam(
				state.left, top,
				lastLineWidth, 1,
				pixels + bufferOffset );
		}
	}

	m_vramTransferState.reset();
	m_transferBuffer.clear();
	EndCommand();
}

uint32_t Gpu::GetHorizontalResolution() const noexcept
{
	static constexpr std::array<uint32_t, 8> Resolutions{ 256, 368, 320, 368, 512, 368, 640, 368 };
	return Resolutions[ m_status.horizontalResolution ];
}

void Gpu::ExecuteCommand() noexcept
{
	const uint32_t value = m_commandBuffer.Peek();
	const uint8_t opcode = static_cast<uint8_t>( value >> 24 );
	switch ( opcode )
	{
		case 0xe1: // draw mode setting
		{
			dbLogDebug( "Gpu::ExecuteCommand() -- set draw mode [%X]", value );
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

			m_commandBuffer.Pop();
			break;
		}

		case 0xe2: // texture window setting
		{
			dbLogDebug( "Gpu::ExecuteCommand() -- set texture window [%X]", value );

			m_textureWindowMaskX = value & 0x1f;
			m_textureWindowMaskY = ( value >> 5 ) & 0x1f;
			m_textureWindowOffsetX = ( value >> 10 ) & 0x1f;
			m_textureWindowOffsetY = ( value >> 15 ) & 0x1f;

			m_renderer.SetTextureWindow( m_textureWindowMaskX, m_textureWindowMaskY, m_textureWindowOffsetX, m_textureWindowOffsetY );

			m_commandBuffer.Pop();
			break;
		}

		case 0xe3: // set draw area top-left
		{
			m_drawAreaLeft = value & 0x3ff;
			m_drawAreaTop = ( value >> 10 ) & 0x1ff;

			dbLogDebug( "Gpu::ExecuteCommand() -- set draw area top-left [%u, %u]", m_drawAreaLeft, m_drawAreaTop );

			m_renderer.SetDrawArea( m_drawAreaLeft, m_drawAreaTop, m_drawAreaRight, m_drawAreaBottom );

			m_commandBuffer.Pop();
			break;
		}

		case 0xe4: // set draw area bottom-right
		{
			m_drawAreaRight = value & 0x3ff;
			m_drawAreaBottom = ( value >> 10 ) & 0x1ff;

			dbLogDebug( "Gpu::ExecuteCommand() -- set draw area bottom-right [%u, %u]", m_drawAreaRight, m_drawAreaBottom );

			m_renderer.SetDrawArea( m_drawAreaLeft, m_drawAreaTop, m_drawAreaRight, m_drawAreaBottom );

			m_commandBuffer.Pop();
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
			dbLogDebug( "Gpu::ExecuteCommand() -- set draw offset [%u, %u]", m_drawOffsetX, m_drawOffsetY );

			m_commandBuffer.Pop();
			break;
		}

		case 0xe6: // mask bit setting
		{
			const bool setMask = value & 0x01;
			const bool checkMask = value & 0x02;
			dbLogDebug( "Gpu::ExecuteCommand() -- set mask bits [set:%i check:%i]", setMask, checkMask );

			m_status.setMaskOnDraw = setMask;
			m_status.checkMaskOnDraw = checkMask;
			m_renderer.SetMaskBits( setMask, checkMask );

			m_commandBuffer.Pop();
			break;
		}

		case 0x01: // clear cache
			dbLogDebug( "Gpu::ExecuteCommand() -- clear GPU cache" );

			m_commandBuffer.Pop();
			break;

		case 0x02: // fill rectangle in VRAM
			dbLogDebug( "Gpu::ExecuteCommand() -- fill rectangle in VRAM" );
			InitCommand( 2, &Gpu::Command_FillRectangle );
			break;

		case 0x80: // copy rectangle (VRAM to VRAM)
			dbLogDebug( "Gpu::ExecuteCommand() -- copy rectangle (VRAM to VRAM)" );
			InitCommand( 3, &Gpu::Command_CopyRectangle );
			break;

		case 0xa0: // copy rectangle (CPU to VRAM)
			dbLogDebug( "Gpu::ExecuteCommand() -- copy rectangle (CPU to VRAM)" );
			InitCommand( 2, &Gpu::Command_WriteToVRam );
			break;

		case 0xc0: // copy rectangle (VRAM to CPU)
			dbLogDebug( "Gpu::ExecuteCommand() -- copy rectangle (VRAM to CPU)" );
			InitCommand( 2, &Gpu::Command_ReadFromVRam );
			break;

		case 0x1f: // interrupt request
		{
			dbLogDebug( "Gpu::ExecuteCommand() -- request interrupt" );
			if ( !m_status.interruptRequest ) // edge triggered
			{
				m_status.interruptRequest = true;
				m_interruptControl.SetInterrupt( Interrupt::Gpu );
			}

			m_commandBuffer.Pop();
			break;
		}

		case 0x03: // unknown. Takes up space in FIFO
			m_commandBuffer.Pop();
			break;

		case 0x00:
		case 0x04:
		case 0x1e:
		case 0xe0:
		case 0xe7:
		case 0xef:
			m_commandBuffer.Pop();
			break; // NOP

		default:
		{
			const RenderCommand command{ value };
			switch ( static_cast<PrimitiveType>( command.type ) )
			{
				case PrimitiveType::Polygon:
				{
					const uint32_t params = ( command.quadPolygon ? 4 : 3 ) * ( 1 + command.textureMapping + command.shading ) - command.shading;
					InitCommand( params, &Gpu::Command_RenderPolygon );
					break;
				}

				case PrimitiveType::Line:
				{
					const uint32_t params = command.shading ? 3 : 2;
					InitCommand( params, &Gpu::Command_RenderLine );

					if ( command.numLines )
					{
						// read vertices into transfer buffer
						m_state = State::PolyLine;
						m_transferBuffer.reserve( 256 );
						m_transferBuffer.push_back( m_commandBuffer.Pop() ); // move command
					}
					break;
				}

				case PrimitiveType::Rectangle:
				{
					const uint32_t params = 1 + static_cast<uint32_t>( command.rectSize == 0 ) + command.textureMapping;
					InitCommand( params, &Gpu::Command_RenderRectangle );
					break;
				}

				default:
				{
					dbLogWarning( "Gpu::ExecuteCommand() -- invalid GP0 opcode [%X]", opcode );
					m_commandBuffer.Pop();
					break;
				}
			}
			break;
		}
	}
}

uint32_t Gpu::GpuRead() noexcept
{
	if ( m_state != State::ReadingVRam )
		return m_gpuRead;

	dbAssert( m_vramTransferState.has_value() );
	dbAssert( !m_vramTransferState->IsFinished() );

	auto getPixel = [this]() -> uint32_t
	{
		const auto x = m_vramTransferState->GetWrappedX();
		const auto y = m_vramTransferState->GetWrappedY();
		m_vramTransferState->Increment();
		return m_vram[ x + y * VRamWidth ];
	};

	uint32_t result = getPixel();

	if ( !m_vramTransferState->IsFinished() )
		result |= getPixel() << 16;

	if ( m_vramTransferState->IsFinished() )
	{
		dbLogDebug( "Gpu::GpuRead_Image -- finished transfer" );
		m_vramTransferState.reset();
		m_state = State::Idle;
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
			dbLog( "Gpu::WriteGP1() -- soft reset" );
			m_crtEvent->UpdateEarly();
			SoftReset();
			break;
		}

		case 0x01: // reset command buffer
			dbLogDebug( "Gpu::WriteGP1() -- clear command buffer" );
			m_crtEvent->UpdateEarly();
			ClearCommandBuffer();
			UpdateDmaRequest();
			break;

		case 0x02: // ack GPU interrupt
			dbLogDebug( "Gpu::WriteGP1() -- acknowledge interrupt" );
			m_status.interruptRequest = false;
			break;

		case 0x03: // display enable
		{
			m_crtEvent->UpdateEarly();
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
			const uint16_t horStart = value & 0xfff;
			const uint16_t horEnd = ( value >> 12 ) & 0xfff;

			if ( horStart != m_horDisplayRangeStart || horEnd != m_horDisplayRangeEnd )
			{
				m_crtEvent->UpdateEarly();
				dbLogDebug( "Gpu::WriteGP1() -- set horizontal display range [%u, %u]", horStart, horEnd );
				m_horDisplayRangeStart = horStart;
				m_horDisplayRangeEnd = horEnd;
				ScheduleCrtEvent();
			}
			break;
		}

		case 0x07: // vertical display range
		{
			const uint16_t verStart = value & 0x3ff;
			const uint16_t verEnd = ( value >> 10 ) & 0x3ff;

			if ( verStart != m_verDisplayRangeStart || verEnd != m_verDisplayRangeEnd )
			{
				m_crtEvent->UpdateEarly();
				dbLogDebug( "Gpu::WriteGP1() -- set vertical display range [%u, %u]", verStart, verEnd );
				m_verDisplayRangeStart = verStart;
				m_verDisplayRangeEnd = verEnd;
				ScheduleCrtEvent();
			}
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
				m_crtEvent->UpdateEarly();

				const bool videoModeChanged = newStatus.videoMode != m_status.videoMode;

				m_status.value = newStatus.value;

				m_renderer.SetDisplaySize( GetHorizontalResolution(), GetVerticalResolution() );
				m_renderer.SetColorDepth( static_cast<DisplayAreaColorDepth>( newStatus.displayAreaColorDepth ) );

				static constexpr std::array<uint16_t, 8> DotClockDividers{ 10, 7, 8, 7, 5, 7, 4, 7 };
				m_crtState.dotClockDivider = DotClockDividers[ newStatus.horizontalResolution ];

				if ( videoModeChanged )
					UpdateCrtConstants();

				ScheduleCrtEvent();
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
	// update the CRT state if it can affect the even/odd status bit
	cycles_t fractionalCycles = m_crtState.fractionalCycles;
	const cycles_t currentGpuCycleInScanline = m_crtState.cycleInScanline + ConvertCpuToGpuCycles( m_crtEvent->GetPendingCycles(), fractionalCycles );
	if ( currentGpuCycleInScanline >= m_crtConstants.cyclesPerScanline )
		m_crtEvent->UpdateEarly();

	return m_status.value;
}

void Gpu::UpdateDmaRequest() noexcept
{
	// readyToReceiveDmaBlock can be set even when reading VRAM
	// JaCzekanski's GPU bandwidth test relies on this behaviour
	const bool canExecuteCommand = m_commandBuffer.Empty() && ( m_pendingCommandCycles < MaxRunAheadCommandCycles );
	m_status.readyToReceiveDmaBlock = ( m_state != State::Parameters ) && ( m_state != State::PolyLine ) && canExecuteCommand;
	m_status.readyToReceiveCommand = ( m_state == State::Idle ) && canExecuteCommand;
	m_status.readyToSendVRamToCpu = ( m_state == State::ReadingVRam );

	// DMA / Data Request, meaning depends on GP1(04h) DMA Direction:
	//   When GP1( 04h ) = 0 --->Always zero( 0 )
	//   When GP1( 04h ) = 1 --->FIFO State( 0 = Full, 1 = Not Full )
	//   When GP1( 04h ) = 2 --->Same as GPUSTAT.28
	//   When GP1( 04h ) = 3 --->Same as GPUSTAT.27
	bool dmaRequest = false;
	switch ( static_cast<DmaDirection>( m_status.dmaDirection ) )
	{
		case DmaDirection::Off:
			break;

		case DmaDirection::Fifo:
			dmaRequest = !m_commandBuffer.Full(); // Duckstation requests when command buffer is not empty?? This feature probably isn't used anyway
			break;

		case DmaDirection::CpuToGp0:
			dmaRequest = m_status.readyToReceiveDmaBlock;
			break;

		case DmaDirection::GpuReadToCpu:
			dmaRequest = m_status.readyToSendVRamToCpu;
			break;
	}
	m_status.dmaRequest = dmaRequest;
	m_dma->SetRequest( Dma::Channel::Gpu, dmaRequest );
}

void Gpu::Command_FillRectangle() noexcept
{
	// not affected by mask settings

	const Color color{ m_commandBuffer.Pop() };
	auto[ x, y ] = DecodeFillPosition( m_commandBuffer.Pop() );
	auto[ width, height ] = DecodeFillSize( m_commandBuffer.Pop() );

	dbLogDebug( "Gpu::Command_FillRectangle() -- pos: %u,%u size: %u,%u", x, y, width, height );

	if ( width > 0 && height > 0 )
	{
		const float r = static_cast<float>( color.r ) / 255.0f;
		const float g = static_cast<float>( color.g ) / 255.0f;
		const float b = static_cast<float>( color.b ) / 255.0f;
		m_renderer.FillVRam( x, y, width, height, r, g, b, 0.0f );
	}

	m_pendingCommandCycles += 46 + ( width / 8 + 9 ) * height; // formula from Duckstation
	EndCommand();
}

void Gpu::Command_CopyRectangle() noexcept
{
	// affected by mask settings

	m_commandBuffer.Pop(); // pop command
	auto[ srcX, srcY ] = DecodeCopyPosition( m_commandBuffer.Pop() );
	auto[ destX, destY ] = DecodeCopyPosition( m_commandBuffer.Pop() );
	auto[ width, height ] = DecodeCopySize( m_commandBuffer.Pop() );

	dbLogDebug( "Gpu::Command_CopyRectangle() -- srcPos: %u,%u destPos: %u,%u size: %u,%u", srcX, srcY, destX, destY, width, height );

	m_renderer.CopyVRam( srcX, srcY, destX, destY, width, height );

	m_pendingCommandCycles += width * height * 2; // formula from Duckstation
	EndCommand();
}

void Gpu::Command_WriteToVRam() noexcept
{
	// affected by mask settings
	SetupVRamCopy();
	auto& state = *m_vramTransferState;

	dbLogDebug( "Gpu::Command_WriteToVram() -- pos: %u,%u size: %u,%u", state.left, state.top, state.width, state.height );

	m_remainingParamaters = ( state.width * state.height + 1 ) / 2; // convert number of pixels to words (rounded up)
	m_transferBuffer.reserve( m_remainingParamaters );
	m_state = State::WritingVRam;
}

void Gpu::Command_ReadFromVRam() noexcept
{
	SetupVRamCopy();
	auto& state = *m_vramTransferState;

	dbLogDebug( "Gpu::Command_ReadFromVram() -- pos: %u,%u size: %u,%u", state.left, state.top, state.width, state.height );

	m_renderer.ReadVRam( state.left, state.top, state.width, state.height, m_vram.get() );
	m_state = State::ReadingVRam;
}

void Gpu::Command_RenderPolygon() noexcept
{
	Vertex vertices[ 4 ];

	const RenderCommand command = m_commandBuffer.Pop();

	// Numbers from Duckstation
	static constexpr uint32_t CommandCycles[ 2 ][ 2 ][ 2 ] = { { { 46, 226 }, { 334, 496 } }, { { 82, 262 }, { 370, 532 } } };
	m_pendingCommandCycles += CommandCycles[ command.quadPolygon ][ command.shading ][ command.textureMapping ];

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

	vertices[ 0 ].position = Position{ m_commandBuffer.Pop() };

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

	vertices[ 1 ].position = Position{ m_commandBuffer.Pop() };

	if ( command.textureMapping )
	{
		const auto value = m_commandBuffer.Pop();
		vertices[ 1 ].texCoord = TexCoord{ value };
		texPage = static_cast<uint16_t>( value >> 16 );
		m_status.SetTexPage( texPage );
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

		vertices[ i ].position = Position{ m_commandBuffer.Pop() };

		if ( command.textureMapping )
			vertices[ i ].texCoord = TexCoord{ m_commandBuffer.Pop() };
	}

	// TODO: check for large polygons

	for ( size_t i = 0; i < numVertices; ++i )
	{
		vertices[ i ].position.x += m_drawOffsetX;
		vertices[ i ].position.y += m_drawOffsetY;
	}

	AddTriangleCommandCycles(
		vertices[ 0 ].position.x, vertices[ 0 ].position.y,
		vertices[ 1 ].position.x, vertices[ 1 ].position.y,
		vertices[ 2 ].position.x, vertices[ 2 ].position.y,
		command.textureMapping,
		command.semiTransparency );

	if ( command.quadPolygon )
	{
		AddTriangleCommandCycles(
			vertices[ 1 ].position.x, vertices[ 1 ].position.y,
			vertices[ 2 ].position.x, vertices[ 2 ].position.y,
			vertices[ 3 ].position.x, vertices[ 3 ].position.y,
			command.textureMapping,
			command.semiTransparency );
	}

#if GPU_RENDER_POLYGONS
	const bool dither = m_status.dither && ( command.shading || ( command.textureMapping && !command.textureMode ) );
	m_renderer.SetDrawMode( texPage, clut, dither );
	m_renderer.PushTriangle( vertices, command.semiTransparency );
	if ( command.quadPolygon )
		m_renderer.PushTriangle( vertices + 1, command.semiTransparency );
#endif

	EndCommand();
}

void Gpu::Command_RenderLine() noexcept
{
	// TODO
	const RenderCommand command{ m_commandBuffer.Pop() };
	m_commandBuffer.Ignore( command.shading ? 3 : 2 );

	m_pendingCommandCycles += 16; // Number from Duckstation
	EndCommand();
}

void Gpu::Command_RenderPolyLine() noexcept
{
	// TODO
	m_transferBuffer.clear();

	m_pendingCommandCycles += 16; // Number from Duckstation
	EndCommand();
}

void Gpu::Command_RenderRectangle() noexcept
{
	// FlushVRam();

	Vertex vertices[ 4 ];

	m_pendingCommandCycles += 16; // Number from Duckstation

	const RenderCommand command = m_commandBuffer.Pop();

	// set color
	const bool noBlend = command.textureMode && command.textureMapping;
	const Color color{ noBlend ? 0x808080 : command.color };
	for ( auto& v : vertices )
		v.color = color;

	// get position
	const Position pos = Position{ m_commandBuffer.Pop() } + Position{ m_drawOffsetX, m_drawOffsetY };

	// get tex coord/set clut
	TexCoord texcoord;

	TexPage texPage = m_status.GetTexPage();
	ClutAttribute clut;

	if ( command.textureMapping )
	{
		const uint32_t value = m_commandBuffer.Pop();

		texcoord = TexCoord{ value };

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
				// size is the last param. Safe to end command here
				EndCommand();
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
		vertices[ 0 ].texCoord = texcoord;
		vertices[ 1 ].texCoord = TexCoord{ texcoord.u,										static_cast<uint16_t>( texcoord.v + height - 1 ) };
		vertices[ 2 ].texCoord = TexCoord{ static_cast<uint16_t>( texcoord.u + width - 1 ),	texcoord.v };
		vertices[ 3 ].texCoord = TexCoord{ static_cast<uint16_t>( texcoord.u + width - 1 ),	static_cast<uint16_t>( texcoord.v + height - 1 ) };
	}

	AddRectangleCommandCycles( width, height, command.textureMapping, command.semiTransparency );

#if GPU_RENDER_RECTANGLES
	m_renderer.SetDrawMode( texPage, clut, false );
	m_renderer.PushQuad( vertices, command.semiTransparency );
#endif

	EndCommand();
}

void Gpu::UpdateCrtConstants() noexcept
{
	m_crtConstants = m_status.videoMode ? PALConstants : NTSCConstants;

	m_crtState.scanline %= m_crtConstants.totalScanlines;
	m_crtState.cycleInScanline %= m_crtConstants.cyclesPerScanline;

	m_crtState.hblank = m_crtState.cycleInScanline < m_horDisplayRangeStart || m_crtState.cycleInScanline >= m_horDisplayRangeEnd;
	m_crtState.vblank = m_crtState.scanline < m_verDisplayRangeStart || m_crtState.scanline >= m_verDisplayRangeEnd;

	m_timers->GetTimer( DotTimerIndex ).UpdateBlank( m_crtState.hblank );
	m_timers->GetTimer( HBlankTimerIndex ).UpdateBlank( m_crtState.vblank );
}

void Gpu::UpdateCrtCycles( cycles_t cpuCycles ) noexcept
{
	dbExpects( cpuCycles <= m_cachedCyclesUntilNextEvent );
	const cycles_t gpuCycles = ConvertCpuToGpuCycles( cpuCycles, m_crtState.fractionalCycles );

	auto& dotTimer = m_timers->GetTimer( DotTimerIndex );
	if ( !dotTimer.IsUsingSystemClock() )
	{
		m_crtState.dotFraction += static_cast<uint32_t>( gpuCycles );
		const uint32_t dots = m_crtState.dotFraction / m_crtState.dotClockDivider;
		m_crtState.dotFraction %= m_crtState.dotClockDivider;
		if ( dots > 0 )
			dotTimer.Update( dots );
	}

	// add cycles
	const uint32_t prevCycleInScanline = m_crtState.cycleInScanline;
	m_crtState.cycleInScanline += gpuCycles;
	const uint32_t finishedScanlines = m_crtState.cycleInScanline / m_crtConstants.cyclesPerScanline;
	m_crtState.cycleInScanline %= m_crtConstants.cyclesPerScanline;

	auto& hblankTimer = m_timers->GetTimer( HBlankTimerIndex );
	if ( !hblankTimer.IsUsingSystemClock() )
	{
		// count how many time cycle has crossed hor display range end since last update
		const uint32_t hblanks = finishedScanlines + uint32_t( prevCycleInScanline < m_horDisplayRangeEnd ) + uint32_t( m_crtState.cycleInScanline >= m_horDisplayRangeEnd ) - 1;
		hblankTimer.Update( hblanks );
	}

	const bool hblank = m_crtState.cycleInScanline < m_horDisplayRangeStart || m_crtState.cycleInScanline >= m_horDisplayRangeEnd;
	if ( m_crtState.hblank != hblank )
	{
		m_crtState.hblank = hblank;
		dotTimer.UpdateBlank( hblank );
	}

	for ( uint32_t scanlinesToDraw = finishedScanlines; scanlinesToDraw > 0; )
	{
		const uint32_t prevScanline = m_crtState.scanline;
		const uint32_t curScanlinesToDraw = std::min( scanlinesToDraw, m_crtConstants.totalScanlines - prevScanline );
		scanlinesToDraw -= curScanlinesToDraw;
		m_crtState.scanline += curScanlinesToDraw;
		dbAssert( m_crtState.scanline <= m_crtConstants.totalScanlines );

		if ( prevScanline < m_verDisplayRangeStart && m_crtState.scanline >= m_verDisplayRangeEnd )
		{
			// skipped over vertical display range, set vblank to false
			m_crtState.vblank = false;
		}

		const bool vblank = m_crtState.scanline < m_verDisplayRangeStart || m_crtState.scanline >= m_verDisplayRangeEnd;
		if ( m_crtState.vblank != vblank )
		{
			m_crtState.vblank = vblank;
			hblankTimer.UpdateBlank( vblank );

			if ( vblank )
			{
				dbLogDebug( "VBlank start" );
				m_interruptControl.SetInterrupt( Interrupt::VBlank );
				m_crtState.displayFrame = true;

				// TODO: flush render batch & update display texture here
			}
			else
			{
				dbLogDebug( "VBlank end" );
			}
		}

		if ( m_crtState.scanline == m_crtConstants.totalScanlines )
		{
			m_crtState.scanline = 0;
			if ( m_status.verticalInterlace )
				m_status.interlaceField = !m_status.interlaceField;
			else
				m_status.interlaceField = 0;
		}
	}

	// In 480-lines mode, bit31 changes per frame. And in 240-lines mode, the bit changes per scanline.
	// The bit is always zero during Vblank (vertical retrace and upper/lower screen border).
	m_status.evenOddVblank = !m_crtState.vblank && ( m_status.Is480iMode() ? (bool)m_status.interlaceField : (bool)( m_crtState.scanline & 1u ) );

	ScheduleCrtEvent();
}

void Gpu::ScheduleCrtEvent() noexcept
{
	cycles_t gpuCycles = std::numeric_limits<cycles_t>::max();

	auto& dotTimer = m_timers->GetTimer( DotTimerIndex );
	auto& hblankTimer = m_timers->GetTimer( HBlankTimerIndex );

	// schedule dot timer
	if ( !dotTimer.IsUsingSystemClock() && !dotTimer.IsPaused() )
	{
		const cycles_t cyclesUntilIrq = dotTimer.GetTicksUntilIrq() * m_crtState.dotClockDivider - m_crtState.dotFraction;
		gpuCycles = std::min( gpuCycles, cyclesUntilIrq );
	}

	// schedule hblank timer or dot timer sync
	if ( dotTimer.GetSyncEnable() )
	{
		const cycles_t cyclesUntilHBlankChange = UnitsUntilRangeChange<cycles_t>( m_crtState.cycleInScanline, m_horDisplayRangeStart, m_horDisplayRangeEnd, m_crtConstants.cyclesPerScanline );
		gpuCycles = std::min( gpuCycles, cyclesUntilHBlankChange );
	}
	else if ( !hblankTimer.IsUsingSystemClock() && !hblankTimer.IsPaused() )
	{
		const cycles_t cyclesUntilHBlank = UnitsUntilTrigger<cycles_t>( m_crtState.cycleInScanline, m_horDisplayRangeEnd, m_crtConstants.cyclesPerScanline );
		gpuCycles = std::min( gpuCycles, cyclesUntilHBlank );
	}

	// schedule vblank or hblank timer sync
	uint32_t scanlinesUntilChange = 0;
	if ( hblankTimer.GetSyncEnable() )
	{
		scanlinesUntilChange = UnitsUntilRangeChange<uint32_t>( m_crtState.scanline, m_verDisplayRangeStart, m_verDisplayRangeEnd, m_crtConstants.totalScanlines );
	}
	else
	{
		scanlinesUntilChange = UnitsUntilTrigger<uint32_t>( m_crtState.scanline, m_verDisplayRangeEnd, m_crtConstants.totalScanlines );
	}
	const cycles_t cyclesUntilVBlankChange = scanlinesUntilChange * m_crtConstants.cyclesPerScanline - m_crtState.cycleInScanline;
	gpuCycles = std::min( gpuCycles, cyclesUntilVBlankChange );

	// schedule next update
	const cycles_t cpuCycles = ConvertGpuToCpuCycles( gpuCycles, m_crtState.fractionalCycles );
	m_cachedCyclesUntilNextEvent = cpuCycles;
	m_crtEvent->Schedule( cpuCycles );
}

} // namespace PSX