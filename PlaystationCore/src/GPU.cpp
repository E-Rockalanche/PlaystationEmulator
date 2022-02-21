#include "GPU.h"
#include "DMA.h"

#include "EventManager.h"
#include "InterruptControl.h"
#include "Renderer.h"
#include "SaveState.h"
#include "Timers.h"

#include <stdx/assert.h>
#include <stdx/bit.h>

#define GPU_DRAW_POLYGONS true
#define GPU_DRAW_LINES true
#define GPU_DRAW_RECTANGLES true

#define GpuLog( ... ) dbLogDebug( __VA_ARGS__ )

namespace PSX
{

namespace
{

template <typename T>
inline constexpr T UnitsUntilRangeChange( T current, T start, T end, T wrappingSize ) noexcept
{
	return ( current < start ) ? ( start - current ) : ( current < end ) ? ( end - current ) : ( wrappingSize - current + start );
};

template <typename T>
inline constexpr T UnitsUntilTrigger( T current, T trigger, T wrappingSize ) noexcept
{
	return ( current < trigger ) ? ( trigger - current ) : ( wrappingSize - current + trigger );
};

template <typename T>
inline constexpr T FloorTo( T value, T multiple ) noexcept
{
	dbExpects( multiple != 0 );
	return static_cast<T>( ( value / multiple ) * multiple );
}

template <typename T>
inline constexpr std::pair<T, T> MinMax( T lhs, T rhs ) noexcept
{
	return ( lhs < rhs ) ? std::make_pair( lhs, rhs ) : std::make_pair( rhs, lhs );
}

inline constexpr std::pair<uint16_t, uint16_t> DecodeFillPosition( uint32_t gpuParam ) noexcept
{
	const uint16_t x = static_cast<uint16_t>( gpuParam ) & 0x3f0;					// [0, 0x3f0] in steps of 0x10
	const uint16_t y = static_cast<uint16_t>( gpuParam >> 16 ) & VRamHeightMask;	// [0, 0x1ff]
	return { x, y };
}

inline constexpr std::pair<uint16_t, uint16_t> DecodeFillSize( uint32_t gpuParam ) noexcept
{
	const uint16_t w = ( ( static_cast<uint16_t>( gpuParam ) & VRamWidthMask ) + 0x0f ) & ~0x0f;	// [0, 0x400] in steps of 0x10, rounded up
	const uint16_t h = static_cast<uint16_t>( gpuParam >> 16 ) & VRamHeightMask;					// [0, 0x1ff]
	return { w, h };
}

inline constexpr std::pair<uint16_t, uint16_t> DecodeCopyPosition( uint32_t gpuParam ) noexcept
{
	const uint16_t x = static_cast<uint16_t>( gpuParam ) & VRamWidthMask;			// [0, 0x3ff]
	const uint16_t y = static_cast<uint16_t>( gpuParam >> 16 ) & VRamHeightMask;	// [0, 0x1ff]
	return { x, y };
}

inline constexpr std::pair<uint16_t, uint16_t> DecodeCopySize( uint32_t gpuParam ) noexcept
{
	const uint16_t w = ( ( static_cast<uint16_t>( gpuParam ) - 1 ) & VRamWidthMask ) + 1;			// [1, 0x400]
	const uint16_t h = ( ( static_cast<uint16_t>( gpuParam >> 16 ) - 1 ) & VRamHeightMask ) + 1;	// [1, 0x200]
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

	s_renderCommandFunctions[ (size_t)RenderCommandType::Fill ] = &Gpu::Command_FillRectangle;
	s_renderCommandFunctions[ (size_t)RenderCommandType::Copy ] = &Gpu::Command_CopyRectangle;
	s_renderCommandFunctions[ (size_t)RenderCommandType::Write ] = &Gpu::Command_WriteToVRam;
	s_renderCommandFunctions[ (size_t)RenderCommandType::Read ] = &Gpu::Command_ReadFromVRam;
	s_renderCommandFunctions[ (size_t)RenderCommandType::Polygon ] = &Gpu::Command_RenderPolygon;
	s_renderCommandFunctions[ (size_t)RenderCommandType::Line ] = &Gpu::Command_RenderLine;
	s_renderCommandFunctions[ (size_t)RenderCommandType::Rectangle ] = &Gpu::Command_RenderRectangle;
}

Gpu::~Gpu() = default;

void Gpu::ClearCommandBuffer() noexcept
{
	if ( m_state == State::WritingVRam )
		FinishVRamWrite();

	m_state = State::Idle;
	m_commandBuffer.Clear();
	m_remainingParamaters = 0;
	m_renderCommandType = RenderCommandType::None;

	m_transferBuffer.clear();
	m_vramTransferState.reset();
}

void Gpu::SoftReset() noexcept
{
	ClearCommandBuffer();

	// reset GPUSTAT
	m_status.value = 0x14802000;
	m_renderer.SetSemiTransparencyMode( m_status.GetSemiTransparencyMode() );
	m_renderer.SetMaskBits( m_status.setMaskOnDraw, m_status.checkMaskOnDraw );
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
	m_renderer.Reset();

	m_pendingCommandCycles = 0;
	m_processingCommandBuffer = false;

	m_gpuRead = 0;

	m_crtState = CrtState{};

	// clear VRAM
	std::fill_n( m_vram.get(), VRamWidth * VRamHeight, uint16_t{ 0 } );

	// reset buffers
	m_commandBuffer.Reset();
	m_transferBuffer.clear();
	m_transferBuffer.shrink_to_fit();

	SoftReset();
}

float Gpu::GetRefreshRate() const noexcept
{
	constexpr float GpuCyclesPerSecond = static_cast<float>( ConvertCpuToGpuCycles( CpuCyclesPerSecond ) );
	const float gpuCyclesPerframe = static_cast<float>( m_crtConstants.totalScanlines ) * static_cast<float>( m_crtConstants.cyclesPerScanline );
	return GpuCyclesPerSecond / gpuCyclesPerframe;
}

float Gpu::GetAspectRatio() const noexcept
{
	constexpr float DefaultAspectRatio = 4.0f / 3.0f;

	const float horCustomRange = static_cast<float>( m_crtState.visibleCycleEnd - m_crtState.visibleCycleStart );
	const float verCustomRange = static_cast<float>( m_crtState.visibleScanlineEnd - m_crtState.visibleScanlineEnd );

	if ( horCustomRange <= 0 || verCustomRange <= 0 )
		return DefaultAspectRatio;

	const float horCrtRange = static_cast<float>( m_crtConstants.visibleCycleEnd - m_crtConstants.visibleCycleStart );
	const float verCrtRange = static_cast<float>( m_crtConstants.visibleScanlineEnd - m_crtConstants.visibleScanlineEnd );

	return DefaultAspectRatio * ( horCustomRange / verCustomRange ) / ( horCrtRange / verCrtRange );
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
						std::invoke( s_renderCommandFunctions[ (size_t)m_renderCommandType ], this );
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
						GpuLog( "Gpu::GP0_Image -- transfer finished" );
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

void Gpu::InitCommand( uint32_t paramaterCount, RenderCommandType renderCommandType ) noexcept
{
	dbExpects( m_state == State::Idle );
	dbExpects( paramaterCount > 0 );
	dbExpects( renderCommandType != RenderCommandType::None );

	m_remainingParamaters = paramaterCount;
	m_renderCommandType = renderCommandType;
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
			GpuLog( "Gpu::ExecuteCommand() -- set draw mode [%X]", value );
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
			GpuLog( "Gpu::ExecuteCommand() -- set texture window [%X]", value );

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

			GpuLog( "Gpu::ExecuteCommand() -- set draw area top-left [%u, %u]", m_drawAreaLeft, m_drawAreaTop );

			m_renderer.SetDrawArea( m_drawAreaLeft, m_drawAreaTop, m_drawAreaRight, m_drawAreaBottom );

			m_commandBuffer.Pop();
			break;
		}

		case 0xe4: // set draw area bottom-right
		{
			m_drawAreaRight = value & 0x3ff;
			m_drawAreaBottom = ( value >> 10 ) & 0x1ff;

			GpuLog( "Gpu::ExecuteCommand() -- set draw area bottom-right [%u, %u]", m_drawAreaRight, m_drawAreaBottom );

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
			GpuLog( "Gpu::ExecuteCommand() -- set draw offset [%u, %u]", m_drawOffsetX, m_drawOffsetY );

			m_commandBuffer.Pop();
			break;
		}

		case 0xe6: // mask bit setting
		{
			const bool setMask = value & 0x01;
			const bool checkMask = value & 0x02;
			GpuLog( "Gpu::ExecuteCommand() -- set mask bits [set:%i check:%i]", setMask, checkMask );

			m_status.setMaskOnDraw = setMask;
			m_status.checkMaskOnDraw = checkMask;
			m_renderer.SetMaskBits( setMask, checkMask );

			m_commandBuffer.Pop();
			break;
		}

		case 0x01: // clear cache
			GpuLog( "Gpu::ExecuteCommand() -- clear GPU cache" );

			m_commandBuffer.Pop();
			break;

		case 0x02: // fill rectangle in VRAM
			InitCommand( 2, RenderCommandType::Fill );
			break;

		case 0x80: // copy rectangle (VRAM to VRAM)
			InitCommand( 3, RenderCommandType::Copy );
			break;

		case 0xa0: // copy rectangle (CPU to VRAM)
			InitCommand( 2, RenderCommandType::Write );
			break;

		case 0xc0: // copy rectangle (VRAM to CPU)
			InitCommand( 2, RenderCommandType::Read );
			break;

		case 0x1f: // interrupt request
		{
			GpuLog( "Gpu::ExecuteCommand() -- request interrupt" );
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
					InitCommand( params, RenderCommandType::Polygon );
					break;
				}

				case PrimitiveType::Line:
				{
					const uint32_t params = command.shading ? 3 : 2;
					InitCommand( params, RenderCommandType::Line );

					if ( command.numLines )
					{
						dbAssert( m_transferBuffer.empty() );

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
					InitCommand( params, RenderCommandType::Rectangle );
					break;
				}

				default:
				{
					dbBreakMessage( "Gpu::ExecuteCommand() -- invalid GP0 opcode [%X]", opcode );
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
		GpuLog( "Gpu::GpuRead_Image -- finished transfer" );
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
			GpuLog( "Gpu::WriteGP1() -- soft reset" );
			m_crtEvent->UpdateEarly();
			SoftReset();
			break;
		}

		case 0x01: // reset command buffer
			GpuLog( "Gpu::WriteGP1() -- clear command buffer" );
			m_crtEvent->UpdateEarly();
			ClearCommandBuffer();
			UpdateDmaRequest();
			break;

		case 0x02: // ack GPU interrupt
			GpuLog( "Gpu::WriteGP1() -- acknowledge interrupt" );
			m_status.interruptRequest = false;
			break;

		case 0x03: // display enable
		{
			m_crtEvent->UpdateEarly();
			const bool disableDisplay = value & 0x1;
			GpuLog( "Gpu::WriteGP1() -- enable display: %s", disableDisplay ? "false" : "true" );
			m_status.displayDisable = disableDisplay;
			m_renderer.SetDisplayEnable( !disableDisplay );
			break;
		}

		case 0x04: // DMA direction / data request
		{
			uint32_t newDirection = value & 0x3;
			GpuLog( "Gpu::WriteGP1() -- set DMA direction: %u", newDirection );

			if ( m_status.dmaDirection != newDirection )
			{
				m_status.dmaDirection = newDirection;
				UpdateDmaRequest();
			}
			break;
		}

		case 0x05: // start of display area
		{
			const uint16_t displayAreaStartX = value & 0x3fe;
			const uint16_t displayAreaStartY = ( value >> 10 ) & 0x1ff;
			if ( m_displayAreaStartX != displayAreaStartX || m_displayAreaStartY != displayAreaStartY )
			{
				GpuLog( "Gpu::WriteGP1() -- set display area start [%u, %u]", displayAreaStartX, displayAreaStartY );
				m_displayAreaStartX = displayAreaStartX;
				m_displayAreaStartY = displayAreaStartY;
				UpdateCrtDisplay();
			}
			break;
		}

		case 0x06: // horizontal display range
		{
			const uint16_t horStart = value & 0xfff;
			const uint16_t horEnd = ( value >> 12 ) & 0xfff;

			if ( horStart != m_horDisplayRangeStart || horEnd != m_horDisplayRangeEnd )
			{
				m_crtEvent->UpdateEarly();
				GpuLog( "Gpu::WriteGP1() -- set horizontal display range [%u, %u]", horStart, horEnd );
				m_horDisplayRangeStart = horStart;
				m_horDisplayRangeEnd = horEnd;
				UpdateCrtDisplay();
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
				GpuLog( "Gpu::WriteGP1() -- set vertical display range [%u, %u]", verStart, verEnd );
				m_verDisplayRangeStart = verStart;
				m_verDisplayRangeEnd = verEnd;
				UpdateCrtDisplay();
				ScheduleCrtEvent();
			}
			break;
		}

		case 0x08: // display mode
		{
			// set resolution, video mode, color depth, interlacing, reverse flag

			GpuLog( "Gpu::WriteGP1() -- set display mode [%X]", value );

			const Status oldStatus = m_status;

			// bits 0-5 same as GPUSTAT bits 17-22
			stdx::masked_set<uint32_t>( m_status.value, 0x3f << 17, value << 17 );
			m_status.horizontalResolution2 = ( value >> 6 ) & 1;
			m_status.reverseFlag = ( value >> 7 ) & 1;

			// update cycles and renderer if the new status is different
			if ( oldStatus.value != m_status.value )
			{
				m_crtEvent->UpdateEarly();

				m_renderer.SetColorDepth( static_cast<DisplayAreaColorDepth>( m_status.displayAreaColorDepth ) );

				const bool videoModeChanged = oldStatus.videoMode != m_status.videoMode;
				const bool resolutionChanged = oldStatus.horizontalResolution != m_status.horizontalResolution ||
					oldStatus.verticalResolution != m_status.verticalResolution ||
					oldStatus.verticalInterlace != m_status.verticalInterlace;

				if ( videoModeChanged )
					UpdateCrtConstants();
				else if ( resolutionChanged )
					UpdateCrtDisplay();

				ScheduleCrtEvent();
			}
			break;
		}

		case 0x09: // new texture disable
		{
			GpuLog( "Gpu::WriteGP1() -- set texture disable [%X]", value );
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

	GpuLog( "Gpu::Command_FillRectangle() -- pos: %u,%u size: %u,%u color: $%02x%02x%02x", x, y, width, height, color.r, color.g, color.b );

	if ( width > 0 && height > 0 )
		m_renderer.FillVRam( x, y, width, height, color.r, color.g, color.b );

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

	GpuLog( "Gpu::Command_CopyRectangle() -- srcPos: %u,%u destPos: %u,%u size: %u,%u", srcX, srcY, destX, destY, width, height );

	m_renderer.CopyVRam( srcX, srcY, destX, destY, width, height );

	m_pendingCommandCycles += width * height * 2; // formula from Duckstation
	EndCommand();
}

void Gpu::Command_WriteToVRam() noexcept
{
	dbAssert( m_transferBuffer.empty() );

	// affected by mask settings
	SetupVRamCopy();
	auto& state = *m_vramTransferState;

	GpuLog( "Gpu::Command_WriteToVram() -- pos: %u,%u size: %u,%u", state.left, state.top, state.width, state.height );

	m_remainingParamaters = ( state.width * state.height + 1 ) / 2; // convert number of pixels to words (rounded up)
	m_transferBuffer.reserve( m_remainingParamaters );
	m_state = State::WritingVRam;
}

void Gpu::Command_ReadFromVRam() noexcept
{
	SetupVRamCopy();
	auto& state = *m_vramTransferState;

	GpuLog( "Gpu::Command_ReadFromVram() -- pos: %u,%u size: %u,%u", state.left, state.top, state.width, state.height );

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

	ClutAttribute clut;
	if ( command.textureMapping )
	{
		const auto value = m_commandBuffer.Pop();
		vertices[ 0 ].texCoord = TexCoord{ value };

		clut = ClutAttribute{ static_cast<uint16_t>( value >> 16 ) };
		for ( auto& v : vertices )
			v.clut = clut;
	}

	// vertex 2

	if ( command.shading )
		vertices[ 1 ].color = Color{ m_commandBuffer.Pop() };

	vertices[ 1 ].position = Position{ m_commandBuffer.Pop() };

	TexPage texPage;
	if ( command.textureMapping )
	{
		const auto value = m_commandBuffer.Pop();
		vertices[ 1 ].texCoord = TexCoord{ value };
		texPage = TexPage{ static_cast<uint16_t>( value >> 16 ) };
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

	for ( size_t i = 0; i < numVertices; ++i )
	{
		vertices[ i ].position.x += m_drawOffsetX;
		vertices[ i ].position.y += m_drawOffsetY;
	}

	const bool dither = m_status.dither && ( command.shading || ( command.textureMapping && !command.textureMode ) );
	m_renderer.SetDrawMode( texPage, clut, dither );

	const auto [minX12, maxX12] = MinMax( vertices[ 1 ].position.x, vertices[ 2 ].position.x );
	const auto [minY12, maxY12] = MinMax( vertices[ 1 ].position.y, vertices[ 2 ].position.y );
	const int16_t minX012 = std::min( minX12, vertices[ 0 ].position.x );
	const int16_t maxX012 = std::max( maxX12, vertices[ 0 ].position.x );
	const int16_t minY012 = std::min( minY12, vertices[ 0 ].position.y );
	const int16_t maxY012 = std::max( maxY12, vertices[ 0 ].position.y );

	// cull first ppolygon
	if ( ( maxX012 - minX012 ) <= MaxPrimitiveWidth && ( maxY012 - minY012 ) <= MaxPrimitiveHeight )
	{
		AddTriangleCommandCycles(
			vertices[ 0 ].position.x, vertices[ 0 ].position.y,
			vertices[ 1 ].position.x, vertices[ 1 ].position.y,
			vertices[ 2 ].position.x, vertices[ 2 ].position.y,
			command.textureMapping,
			command.semiTransparency );

		GpuLog( "Gpu::Command_RenderPolygon -- (%i, %i), (%i, %i), (%i, %i)",
			vertices[ 0 ].position.x, vertices[ 0 ].position.y,
			vertices[ 1 ].position.x, vertices[ 1 ].position.y,
			vertices[ 2 ].position.x, vertices[ 2 ].position.y );

#if GPU_DRAW_POLYGONS
		m_renderer.PushTriangle( vertices, command.semiTransparency );
#endif
	}
	else
	{
		GpuLog( "Gpu::Command_RenderPolygon -- culling triangle (%i, %i), (%i, %i), (%i, %i)",
			vertices[ 0 ].position.x, vertices[ 0 ].position.y,
			vertices[ 1 ].position.x, vertices[ 1 ].position.y,
			vertices[ 2 ].position.x, vertices[ 2 ].position.y );
	}

	if ( command.quadPolygon )
	{
		const int16_t minX123 = std::min( minX12, vertices[ 3 ].position.x );
		const int16_t maxX123 = std::max( maxX12, vertices[ 3 ].position.x );
		const int16_t minY123 = std::min( minY12, vertices[ 3 ].position.y );
		const int16_t maxY123 = std::max( maxY12, vertices[ 3 ].position.y );

		// cull second polygon
		if ( ( maxX123 - minX123 ) <= MaxPrimitiveWidth && ( maxY123 - minY123 ) <= MaxPrimitiveHeight )
		{
			AddTriangleCommandCycles(
				vertices[ 1 ].position.x, vertices[ 1 ].position.y,
				vertices[ 2 ].position.x, vertices[ 2 ].position.y,
				vertices[ 3 ].position.x, vertices[ 3 ].position.y,
				command.textureMapping,
				command.semiTransparency );

			GpuLog( "Gpu::Command_RenderPolygon -- (%i, %i), (%i, %i), (%i, %i)",
				vertices[ 0 ].position.x, vertices[ 0 ].position.y,
				vertices[ 1 ].position.x, vertices[ 1 ].position.y,
				vertices[ 2 ].position.x, vertices[ 2 ].position.y );

#if GPU_DRAW_POLYGONS
			m_renderer.PushTriangle( vertices + 1, command.semiTransparency );
#endif
		}
		else
		{
			GpuLog( "Gpu::Command_RenderPolygon -- culling triangle (%i, %i), (%i, %i), (%i, %i)",
				vertices[ 1 ].position.x, vertices[ 1 ].position.y,
				vertices[ 2 ].position.x, vertices[ 2 ].position.y,
				vertices[ 3 ].position.x, vertices[ 3 ].position.y );
		}
	}

	EndCommand();
}

void Gpu::Command_RenderLine() noexcept
{
	m_pendingCommandCycles += 16; // Number from Duckstation

	TexPage texPage = m_status.GetTexPage();
	texPage.textureDisable = true;
	m_renderer.SetDrawMode( texPage, ClutAttribute{}, m_status.dither );

	const RenderCommand command{ m_commandBuffer.Pop() };
	const Color c1 = Color{ command.value };
	const Position p1 = Position{ m_commandBuffer.Pop() };
	const Color c2 = command.shading ? Color{ m_commandBuffer.Pop() } : c1;
	const Position p2 = Position{ m_commandBuffer.Pop() };

	Command_RenderLineInternal( p1, c1, p2, c2, texPage, command.semiTransparency );

	EndCommand();
}

void Gpu::Command_RenderPolyLine() noexcept
{
	dbExpects( m_transferBuffer.size() >= 3 );

	m_pendingCommandCycles += 16; // Number from Duckstation

	TexPage texPage = m_status.GetTexPage();
	texPage.textureDisable = true;
	m_renderer.SetDrawMode( texPage, ClutAttribute{}, m_status.dither );

	const RenderCommand command{ m_transferBuffer[ 0 ] };
	Color c1{ command.value };
	Position p1{ m_transferBuffer[ 1 ] };

	for ( size_t i = 2; i < m_transferBuffer.size(); )
	{
		const Color c2 = command.shading ? Color{ m_transferBuffer[ i++ ] } : c1;
		const Position p2{ m_transferBuffer[ i++ ] };

		Command_RenderLineInternal( p1, c1, p2, c2, texPage, command.semiTransparency );

		p1 = p2;
		c1 = c2;
	}

	m_transferBuffer.clear();

	EndCommand();
}

void Gpu::Command_RenderLineInternal( Position p1, Color c1, Position p2, Color c2, TexPage texPage, bool semiTransparent ) noexcept
{
	Vertex vertices[ 4 ];

	const int32_t dx = p2.x - p1.x;
	const int32_t dy = p2.y - p1.y;

	const int32_t absDx = std::abs( dx );
	const int32_t absDy = std::abs( dy );

	// cull lines that are too long
	if ( absDx > MaxPrimitiveWidth || absDy > MaxPrimitiveHeight )
		return;

	GpuLog( "Gpu::Command_RenderLineInternal -- (%i, %i), (%i, %i)", p1.x, p1.y, p2.x, p2.y );

	p1.x += m_drawOffsetX;
	p1.y += m_drawOffsetY;
	p2.x += m_drawOffsetX;
	p2.y += m_drawOffsetY;

	const int32_t clipX1 = std::clamp<int32_t>( p1.x, m_drawAreaLeft, m_drawAreaRight );
	const int32_t clipY1 = std::clamp<int32_t>( p1.y, m_drawAreaTop, m_drawAreaBottom );
	const int32_t clipX2 = std::clamp<int32_t>( p2.x, m_drawAreaLeft, m_drawAreaRight );
	const int32_t clipY2 = std::clamp<int32_t>( p2.y, m_drawAreaTop, m_drawAreaBottom );

	const int32_t clipWidth = std::abs( clipX2 - clipX1 ) + 1;
	const int32_t clipHeight = std::abs( clipY2 - clipY1 ) + 1;
	AddLineCommandCycles( clipWidth, clipHeight );

	if ( dx == 0 && dy == 0 )
	{
		// render a point with first color

		vertices[ 0 ].position = p1;
		vertices[ 1 ].position = Position( p1.x + 1,	p1.y );
		vertices[ 2 ].position = Position( p1.x,		p1.y + 1 );
		vertices[ 3 ].position = Position( p1.x + 1,	p1.y + 1 );

		vertices[ 0 ].color = c1;
		vertices[ 1 ].color = c1;
		vertices[ 2 ].color = c1;
		vertices[ 3 ].color = c1;
	}
	else
	{
		int16_t padX1 = 0;
		int16_t padY1 = 0;
		int16_t padX2 = 0;
		int16_t padY2 = 0;

		int16_t fillDx = 0;
		int16_t fillDy = 0;

		// align ends of line depending on if it's more horizontal or vertical
		if ( absDx > absDy )
		{
			fillDx = 0;
			fillDy = 1;

			if ( dx > 0 )
			{
				// left to right
				padX2 = 1;
			}
			else
			{
				// right to left
				padX1 = 1;
			}
		}
		else
		{
			fillDx = 1;
			fillDy = 0;

			if ( dy > 0 )
			{
				// top to bottom
				padY2 = 1;
			}
			else
			{
				padY1 = 1;
			}
		}

		const int16_t x1 = p1.x + padX1;
		const int16_t y1 = p1.y + padY1;
		const int16_t x2 = p2.x + padX2;
		const int16_t y2 = p2.y + padY2;

		vertices[ 0 ].position = Position( x1,				y1 );
		vertices[ 1 ].position = Position( x1 + fillDx,		y1 + fillDy );
		vertices[ 2 ].position = Position( x2,				y2 );
		vertices[ 3 ].position = Position( x2 + fillDx,		y2 + fillDy );

		vertices[ 0 ].color = c1;
		vertices[ 1 ].color = c1;
		vertices[ 2 ].color = c2;
		vertices[ 3 ].color = c2;
	}

	for ( auto& v : vertices )
		v.texPage = texPage;

#if GPU_DRAW_LINES
	m_renderer.PushQuad( vertices, semiTransparent );
#endif
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

		clut = ClutAttribute{ static_cast<uint16_t>( value >> 16 ) };
		for ( auto& v : vertices )
		{
			v.clut = clut;
			v.texPage = texPage;
		}
	}
	else
	{
		// still need semitransparency mode
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
			width = static_cast<int16_t>( sizeParam & 0xffff );
			height = static_cast<int16_t>( sizeParam >> 16 );

			if ( width == 0 || height == 0 || width > MaxPrimitiveWidth || height > MaxPrimitiveHeight )
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

	const int16_t x2 = pos.x + width;
	const int16_t y2 = pos.y + height;
	vertices[ 0 ].position = pos;
	vertices[ 1 ].position = Position{ x2,		pos.y };
	vertices[ 2 ].position = Position{ pos.x,	y2 };
	vertices[ 3 ].position = Position{ x2,		y2 };

	if ( command.textureMapping )
	{
		int16_t u1, v1, u2, v2;
		if ( m_texturedRectFlipX )
		{
			u1 = texcoord.u;
			u2 = u1 - width;
		}
		else
		{
			u1 = texcoord.u;
			u2 = u1 + width;
		}

		if ( m_texturedRectFlipY )
		{
			v1 = texcoord.v;
			v2 = v1 - height;
		}
		else
		{
			v1 = texcoord.v;
			v2 = v1 + height;
		}

		vertices[ 0 ].texCoord = TexCoord{ u1, v1 };
		vertices[ 1 ].texCoord = TexCoord{ u2, v1 };
		vertices[ 2 ].texCoord = TexCoord{ u1, v2 };
		vertices[ 3 ].texCoord = TexCoord{ u2, v2 };
	}

	AddRectangleCommandCycles( width, height, command.textureMapping, command.semiTransparency );

	GpuLog( "Gpu::Command_RenderRectangle -- (%i, %i), (%i x %i) $%02x%02x%02x", pos.x, pos.y, width, height, color.r, color.g, color.b );

#if GPU_DRAW_RECTANGLES
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

	UpdateCrtDisplay();
}

void Gpu::UpdateCrtDisplay() noexcept
{
	static constexpr std::array<uint16_t, 8> DotClockDividers{ 10, 7, 8, 7, 5, 7, 4, 7 };
	const uint32_t dotClockDivider = m_crtState.dotClockDivider = DotClockDividers[ m_status.horizontalResolution ];

	// clamp and round horizontal display range
	const uint32_t horDisplayRangeStart = FloorTo<uint32_t>( std::min( m_horDisplayRangeStart, m_crtConstants.cyclesPerScanline ), dotClockDivider );
	const uint32_t horDisplayRangeEnd = FloorTo<uint32_t>( std::min( m_horDisplayRangeEnd, m_crtConstants.cyclesPerScanline ), dotClockDivider );

	// clamp vertical display range
	const uint32_t verDisplayRangeStart = std::min( m_verDisplayRangeStart, m_crtConstants.totalScanlines );
	const uint32_t verDisplayRangeEnd = std::min( m_verDisplayRangeEnd, m_crtConstants.totalScanlines );

	// calculate custom visible range
	uint32_t visibleCycleStart = 0;
	uint32_t visibleCycleEnd = 0;
	uint32_t visibleScanlineStart = 0;
	uint32_t visibleScanlineEnd = 0;
	switch ( m_cropMode )
	{
		case CropMode::None:
			// use default CRT constants. May introduce borders or overscan depending on game
			visibleCycleStart = m_crtConstants.visibleCycleStart;
			visibleCycleEnd = m_crtConstants.visibleCycleEnd;
			visibleScanlineStart = m_crtConstants.visibleScanlineStart;
			visibleScanlineEnd = m_crtConstants.visibleScanlineEnd;
			break;

		case CropMode::Fit:
			visibleCycleStart = horDisplayRangeStart;
			visibleCycleEnd = horDisplayRangeEnd;
			visibleScanlineStart = verDisplayRangeStart;
			visibleScanlineEnd = verDisplayRangeEnd;
			break;

		default:
			dbBreak();
			break;
	}

	// clamp custom visible range to CRT visible range
	visibleCycleStart =		std::clamp<uint32_t>( visibleCycleStart,		m_crtConstants.visibleCycleStart,		m_crtConstants.visibleCycleEnd );
	visibleCycleEnd =		std::clamp<uint32_t>( visibleCycleEnd,			visibleCycleStart,						m_crtConstants.visibleCycleEnd );
	visibleScanlineStart =	std::clamp<uint32_t>( visibleScanlineStart,		m_crtConstants.visibleScanlineStart,	m_crtConstants.visibleScanlineEnd );
	visibleScanlineEnd =	std::clamp<uint32_t>( visibleScanlineEnd,		visibleScanlineStart,					m_crtConstants.visibleScanlineEnd );

	// calculate target display size
	const uint32_t heightMultiplier = m_status.verticalInterlace ? 2 : 1;
	const uint32_t targetDisplayWidth = ( visibleCycleEnd - visibleCycleStart ) / dotClockDivider;
	const uint32_t targetDisplayHeight = ( visibleScanlineEnd - visibleScanlineStart ) * heightMultiplier;

	// calculate display width (rounded to 4 pixels)
	const uint32_t horDisplayCycles = ( horDisplayRangeEnd > horDisplayRangeStart ) ? ( horDisplayRangeEnd - horDisplayRangeStart ) : 0;
	uint32_t vramDisplayWidth = FloorTo<uint32_t>( horDisplayCycles / dotClockDivider + 2, 4 );

	// calculate display X
	uint32_t vramDisplayX = 0;
	uint32_t targetDisplayX = 0;
	if ( horDisplayRangeStart >= visibleCycleStart )
	{
		// black border
		vramDisplayX = m_displayAreaStartX;
		targetDisplayX = ( horDisplayRangeStart - visibleCycleStart ) / dotClockDivider;
	}
	else
	{
		// cropped
		const uint32_t cropLeft = ( visibleCycleStart - horDisplayRangeStart ) / dotClockDivider;
		vramDisplayX = ( m_displayAreaStartX + cropLeft ) % VRamWidth;
		targetDisplayX = 0;
		vramDisplayWidth -= cropLeft;
	}

	// crop vram display width to target bounds
	vramDisplayWidth = std::min( vramDisplayWidth, targetDisplayWidth - targetDisplayX );

	// calculate display height
	uint32_t vramDisplayHeight = ( ( m_verDisplayRangeEnd > m_verDisplayRangeStart ) ? ( m_verDisplayRangeEnd - m_verDisplayRangeStart ) : 0 ) * heightMultiplier;

	// calculate display Y
	uint32_t vramDisplayY = 0;
	uint32_t targetDisplayY = 0;
	if ( verDisplayRangeStart >= visibleScanlineStart )
	{
		// black border
		vramDisplayY = m_displayAreaStartY;
		targetDisplayY = ( verDisplayRangeStart - visibleScanlineStart ) * heightMultiplier;
	}
	else
	{
		// cropped
		const uint32_t cropTop = ( visibleScanlineStart - verDisplayRangeStart ) * heightMultiplier;
		vramDisplayY = ( m_displayAreaStartY + cropTop ) % VRamHeight;
		targetDisplayY = 0;
		vramDisplayHeight -= cropTop;
	}

	// crop vram height to target bounds
	vramDisplayHeight = std::min( vramDisplayHeight, targetDisplayHeight - targetDisplayY );

	m_renderer.SetDisplayArea(
		Renderer::DisplayArea{ vramDisplayX, vramDisplayY, vramDisplayWidth, vramDisplayHeight },
		Renderer::DisplayArea{ targetDisplayX, targetDisplayY, targetDisplayWidth, targetDisplayHeight },
		GetAspectRatio() );
}

void Gpu::UpdateCrtCycles( cycles_t cpuCycles ) noexcept
{
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
				GpuLog( "VBLANK START\n\n\n" );
				m_interruptControl.SetInterrupt( Interrupt::VBlank );
				m_crtState.displayFrame = true;

				// TODO: flush render batch & update display texture here
			}
			else
			{
				GpuLog( "VBLANK END\n\n\n" );
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
	m_crtEvent->Schedule( cpuCycles );
}

void Gpu::Serialize( SaveStateSerializer& serializer )
{
	dbAssert( !m_processingCommandBuffer );

	if ( !serializer.Header( "GPU", 1 ) )
		return;

	m_crtEvent->Serialize( serializer );
	m_commandEvent->Serialize( serializer );

	serializer( m_state );
	serializer( m_commandBuffer );
	serializer( m_remainingParamaters );
	serializer( m_renderCommandType );
	serializer( m_pendingCommandCycles );

	serializer( m_gpuRead );
	serializer( m_status.value );

	serializer( m_texturedRectFlipX );
	serializer( m_texturedRectFlipY );

	serializer( m_textureWindowMaskX );
	serializer( m_textureWindowMaskY );
	serializer( m_textureWindowOffsetX );
	serializer( m_textureWindowOffsetY );

	serializer( m_drawAreaLeft );
	serializer( m_drawAreaTop );
	serializer( m_drawAreaRight );
	serializer( m_drawAreaBottom );

	serializer( m_drawOffsetX );
	serializer( m_drawOffsetY );

	serializer( m_displayAreaStartX );
	serializer( m_displayAreaStartY );

	serializer( m_horDisplayRangeStart );
	serializer( m_horDisplayRangeEnd );

	serializer( m_verDisplayRangeStart );
	serializer( m_verDisplayRangeEnd );

	serializer( m_crtConstants.totalScanlines );
	serializer( m_crtConstants.cyclesPerScanline );
	serializer( m_crtConstants.visibleScanlineStart );
	serializer( m_crtConstants.visibleScanlineEnd );
	serializer( m_crtConstants.visibleCycleStart );
	serializer( m_crtConstants.visibleCycleEnd );

	serializer( m_crtState.fractionalCycles );
	serializer( m_crtState.scanline );
	serializer( m_crtState.cycleInScanline );
	serializer( m_crtState.dotClockDivider );
	serializer( m_crtState.dotFraction );
	serializer( m_crtState.visibleCycleStart );
	serializer( m_crtState.visibleCycleEnd );
	serializer( m_crtState.visibleScanlineStart );
	serializer( m_crtState.visibleScanlineEnd );
	serializer( m_crtState.hblank );
	serializer( m_crtState.vblank );
	serializer( m_crtState.evenOddLine );
	serializer( m_crtState.displayFrame );

	serializer( m_crtState.displayFrame );

	bool hasTransferState = m_vramTransferState.has_value();
	serializer( hasTransferState );
	if ( hasTransferState )
	{
		if ( serializer.Reading() )
			m_vramTransferState.emplace();

		serializer( m_vramTransferState->left );
		serializer( m_vramTransferState->top );
		serializer( m_vramTransferState->width );
		serializer( m_vramTransferState->height );
		serializer( m_vramTransferState->dx );
		serializer( m_vramTransferState->dy );
	}

	serializer( m_cropMode );

	if ( serializer.Writing() )
		m_renderer.ReadVRam( 0, 0, VRamWidth, VRamHeight, m_vram.get() );

	serializer( m_vram.get(), VRamWidth * VRamHeight );

	if ( serializer.Reading() )
	{
		m_renderer.Reset();

		m_renderer.UpdateVRam( 0, 0, VRamWidth, VRamHeight, m_vram.get() );

		m_renderer.SetTextureWindow( m_textureWindowMaskX, m_textureWindowMaskY, m_textureWindowOffsetX, m_textureWindowOffsetY );
		m_renderer.SetDrawArea( m_drawAreaLeft, m_drawAreaTop, m_drawAreaRight, m_drawAreaBottom );
		m_renderer.SetSemiTransparencyMode( m_status.GetSemiTransparencyMode() );
		m_renderer.SetMaskBits( m_status.setMaskOnDraw, m_status.checkMaskOnDraw );
		m_renderer.SetColorDepth( m_status.GetDisplayAreaColorDepth() );
		m_renderer.SetDisplayEnable( !m_status.displayDisable );

		UpdateCrtDisplay();
	}
}

} // namespace PSX