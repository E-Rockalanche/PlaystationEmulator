#include "GPU.h"

#include "bit.h"

namespace PSX
{

namespace
{

struct GpuCommand
{
	enum : uint32_t
	{
		TextureMode = 1u << 24, // textured polygon/rect only
		SemiTransparency = 1u << 25, // all render types
		Texturemapping = 1u << 26, // polygon/ract only
		RectSizeMask = 0x3u << 27, // rect only
		NumVertices = 1u << 27, // polygon only
		NumLines = 1u << 27, // line only
		Shading = 1u << 28, // polygon/line only
		PrimitiveTypeMask = 0x7u << 29
	};
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

enum class GpuDisplayControlCommand : uint8_t
{
	ResetGpu = 0x00,
	ResetCommandBuffer = 0x01,
	AckGpuInterrupt = 0x02,
	DisplayEnable = 0x03,
	DmaDirection = 0x04,
	StartOfDisplayArea = 0x05,
	HorizontalDisplayRange = 0x06,
	VerticalDisplayRange = 0x07,
	DisplayMode = 0x08,
	GetGpuInfo = 0x10,
	NewTextureDisable = 0x09,
	SpecialTextureDisable = 0x20,
	Unknown = 0x0b,
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

void Gpu::WriteGP0( uint32_t value ) noexcept
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
			break;

		case 0x80: // copy rectangle (VRAM to VRAM)
			dbLog( "copy rectangle (VRAM to VRAM) [%x]", value );
			break;

		case 0xa0: // copy rectangle (CPU to VRAM)
			dbLog( "copy rectangle (CPU to VRAM) [%x]", value );
			break;

		case 0xc0: // copy rectangle (VRAM to CPU)
			dbLog( "copy rectangle (VRAM to CPU) [%x]", value );
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
			dbBreakMessage( "unhandled GP0 opcode [%x]", opcode );
			break;
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

			// TODO: set display address?
			// TODO: clear FIFO
			// TODO: clear cache?
		}

		case 0x01: // reset command buffer
			dbLog( "reset command buffer" );
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

}