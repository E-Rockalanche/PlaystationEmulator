#include "GPU.h"

#include "bit.h"

namespace PSX
{

namespace
{

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
};

enum class TextureMode
{
	Blended,
	Raw
};

enum class RectangleSize
{
	Variable,
	OneByOne,
	EightByEight,
	SixteenBySixteen,
};

enum class PrimitiveType
{
	Polygon = 1,
	Line = 2,
	Rectangle = 3,
};

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

}

void Gpu::Reset()
{
	m_gp0Mode = &Gpu::GP0Command;

	ClearCommandBuffer();

	m_gpuRead = 0;

	m_status.value = 0;
	m_status.displayDisable = true;
	m_status.readyToReceiveCommand = true;
	m_status.readyToReceiveDmaBlock = true;
	m_status.readyToSendVRamToCpu = true;

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

	m_drawOffsetX = 0;
	m_drawOffsetY = 0;

	m_displayAreaStartX = 0;
	m_displayAreaStartY = 0;

	m_horDisplayRange1 = 0;
	m_horDisplayRange2 = 0;

	m_verDisplayRange1 = 0;
	m_verDisplayRange2 = 0;

	m_vram.Fill( char( -1 ) );
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

		default:
			dbBreak();
			return 0;
	}
}

void Gpu::GP0Command( uint32_t value ) noexcept
{
	const uint8_t opcode = static_cast<uint8_t>( value >> 24 );
	switch ( opcode )
	{
		case 0xe1: // draw mode setting
		{
			static constexpr uint32_t WriteMask = 0x3f;
			m_status.value = ( m_status.value & ~WriteMask ) | ( value & WriteMask );

			m_status.textureDisable = ( value >> 11 ) & 1;

			m_texturedRectFlipX = ( value >> 12 ) & 1;
			m_texturedRectFlipY = ( value >> 13 ) & 1;
			break;
		}

		case 0xe2: // texture window setting
		{
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
			break;
		}

		case 0xe4: // set draw area bottom-right
		{
			m_drawAreaRight = value & 0x3ff;
			m_drawAreaBottom = ( value >> 10 ) & 0x1ff;
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
			break;
		}

		case 0xe6: // mask bit setting
		{
			m_status.setMaskOnDraw = value & 1;
			m_status.checkMaskOnDraw = ( value >> 1 ) & 1;
			break;
		}

		case 0x01: // clear cache
			dbLog( "clear GPU cache" );
			break;

		case 0x02: // fill rectangle in VRAM
			dbLog( "fill rectangle in VRAM [%x]", value );
			InitCommand( value, 2, &Gpu::FillRectangle );
			break;

		case 0x80: // copy rectangle (VRAM to VRAM)
			dbLog( "copy rectangle (VRAM to VRAM) [%x]", value );
			InitCommand( value, 3, &Gpu::CopyRectangle );
			break;

		case 0xa0: // copy rectangle (CPU to VRAM)
			dbLog( "copy rectangle (CPU to VRAM) [%x]", value );
			InitCommand( value, 2, &Gpu::CopyRectangleToVram );
			break;

		case 0xc0: // copy rectangle (VRAM to CPU)
			dbLog( "copy rectangle (VRAM to CPU) [%x]", value );
			InitCommand( value, 2, &Gpu::CopyRectangleFromVram );
			break;

		case 0x1f: // interrupt request
			m_status.interruptRequest = false;
			break;

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

					InitCommand( value, params, &Gpu::TempFinishCommandParams );
					break;
				}

				case PrimitiveType::Line:
				{
					m_commandBuffer.Push( value );
					m_commandFunction = &Gpu::TempFinishCommandParams;
					if ( value & RenderCommand::NumLines )
					{
						m_gp0Mode = &Gpu::GP0PolyLine;
					}
					else
					{
						m_remainingWords = ( value & RenderCommand::Shading ) ? 3 : 2;
						m_gp0Mode = &Gpu::GP0Params;
					}

					break;
				}

				case PrimitiveType::Rectangle:
				{
					const auto rectSize = static_cast<RectangleSize>( ( value & RenderCommand::RectSizeMask ) >> 27 );
					const bool textureMapping = value & RenderCommand::TextureMapping;

					const uint32_t params = 1 + ( rectSize == RectangleSize::Variable ) + textureMapping;
					InitCommand( value, params, &Gpu::TempFinishCommandParams );
					break;
				}

				default:
					dbBreakMessage( "unhandled GP0 opcode [%x]", opcode );
					break;
			}
			break;
		}
	}
}

void Gpu::GP0Params( uint32_t param ) noexcept
{
	dbExpects( m_remainingWords > 0 );
	m_commandBuffer.Push( param );
	if ( --m_remainingWords == 0 )
	{
		std::invoke( m_commandFunction, this );

		// command function must clear buffer and set GP0 mode
	}
}

void Gpu::GP0PolyLine( uint32_t param ) noexcept
{
	if ( param != 0x55555555 )
	{
		m_commandBuffer.Push( param );
	}
	else
	{
		std::invoke( m_commandFunction, this );
		ClearCommandBuffer();
	}
}

void Gpu::GP0ImageLoad( uint32_t ) noexcept
{
	dbExpects( m_remainingWords > 0 );
	if ( --m_remainingWords == 0 )
	{
		ClearCommandBuffer();
	}
}

void Gpu::WriteGP1( uint32_t value ) noexcept
{
	const uint8_t opcode = static_cast<uint8_t>( value >> 24 );
	switch ( opcode )
	{
		case 0x00: // reset GPU
		{
			m_status.texturePageBaseX = 0;
			m_status.texturePageBaseY = 0;
			m_status.semiTransparency = 0;
			m_status.texturePageColors = 0;
			m_status.dither = false;
			m_status.drawToDisplayArea = false;
			m_status.textureDisable = false;
			m_status.interruptRequest = false;
			m_status.displayDisable = true;
			m_status.dmaDirection = DmaDirection::Off;
			m_status.setMaskOnDraw = false;
			m_status.checkMaskOnDraw = false;

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

			m_drawOffsetX = 0;
			m_drawOffsetY = 0;

			m_horDisplayRange1 = 0x200;
			m_horDisplayRange2 = 0x200 + 256 * 10;
			m_verDisplayRange1 = 0x10;
			m_verDisplayRange2 = 0x10 + 240;

			m_displayAreaStartX = 0;
			m_displayAreaStartY = 0;

			ClearCommandBuffer();

			// TODO: clear cache?

			break;
		}

		case 0x01: // reset command buffer
			ClearCommandBuffer();
			break;

		case 0x02: // ack GPU interrupt
			m_status.interruptRequest = false;
			break;

		case 0x03: // display enable
			m_status.displayDisable = value & 1;
			break;

		case 0x04: // DMA direction / data request
		{
			m_status.dmaDirection = value & 0x3;
			break;
		}

		case 0x05: // start of display area
		{
			m_displayAreaStartX = value & 0x3fe;
			m_displayAreaStartY = ( value >> 10 ) & 0x1ff;
			break;
		}

		case 0x06: // horizontal display range
		{
			m_horDisplayRange1 = value & 0xfff;
			m_horDisplayRange2 = ( value >> 12 ) & 0xfff;
			break;
		}

		case 0x07: // vertical display range
		{
			m_verDisplayRange1 = value & 0x3ff;
			m_verDisplayRange2 = ( value >> 10 ) & 0x3ff;
			break;
		}

		case 0x08: // display mode
		{
			static constexpr uint32_t WriteMask = 0x3f << 17;
			m_status.value = ( m_status.value & ~WriteMask ) | ( ( value << 17 ) & WriteMask );
			m_status.horizontalResolution2 = ( value >> 6 ) & 1;
			m_status.reverseFlag = ( value >> 7 ) & 1;
			break;
		}

		case 0x10: // get GPU info
		{
			switch ( value & 0x7 )
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

void Gpu::FillRectangle() noexcept
{
	dbBreak();
}

void Gpu::CopyRectangle() noexcept
{
	dbBreak();
}

void Gpu::CopyRectangleToVram() noexcept
{
	// dimensions counted in halfwords
	auto& dimensions = m_commandBuffer[ 2 ];
	const uint32_t width = dimensions & 0xffff;
	const uint32_t height = dimensions >> 16;

	const uint32_t halfwords = width * height;
	m_remainingWords = halfwords / 2;
	if ( halfwords % 1 )
		++m_remainingWords;

	m_gp0Mode = &Gpu::GP0ImageLoad;
}

void Gpu::CopyRectangleFromVram() noexcept
{
	auto& dimensions = m_commandBuffer[ 2 ];
	const uint32_t width = dimensions & 0xffff;
	const uint32_t height = dimensions >> 16;

	dbLog( "copy rectangle from VRAM to CPU [%ux%u]", width, height );

	// TODO: provide data on GPUREAD

	ClearCommandBuffer();
}

} // namespace PSX