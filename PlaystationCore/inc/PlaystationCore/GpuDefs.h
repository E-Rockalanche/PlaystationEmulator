#pragma once

#include <stdx/compiler.h>

#include <cstdint>

namespace PSX
{

constexpr uint32_t VRamWidth = 1024;
constexpr uint32_t VRamHeight = 512;

constexpr float VRamWidthF = static_cast<float>( VRamWidth );
constexpr float VRamHeightF = static_cast<float>( VRamHeight );

constexpr uint32_t VRamWidthMask = VRamWidth - 1;
constexpr uint32_t VRamHeightMask = VRamHeight - 1;

constexpr uint32_t TexturePageWidth = 256;
constexpr uint32_t TexturePageHeight = 256;

constexpr uint32_t TexturePageBaseXMult = 64;
constexpr uint32_t TexturePageBaseYMult = 256;

constexpr uint32_t ClutWidth = 256;
constexpr uint32_t ClutHeight = 1;

constexpr uint32_t ClutBaseXMult = 16;
constexpr uint32_t ClutBaseYMult = 1;

enum class SemiTransparencyMode
{
	Blend, // B/2 + F/2
	Add, // B + F
	ReverseSubtract, // B - F
	AddQuarter // B + F/4
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

enum class VideoMode
{
	NTSC,
	PAL
};

enum class VerticalResolution : uint8_t
{
	P240,
	P480
};

enum class DisplayAreaColorDepth : uint8_t
{
	B15,
	B24
};

enum class DmaDirection
{
	Off,
	Fifo,
	CpuToGP0,
	GpuReadToCpu
};

union PositionParameter
{
	explicit constexpr PositionParameter( uint32_t param ) : value{ param } {}

	struct
	{
		int16_t x : 11;
		int16_t : 5;
		int16_t y : 11;
		int16_t : 5;
	};
	uint32_t value;
};
static_assert( sizeof( PositionParameter ) == 4 );

struct Position
{
	constexpr Position() = default;
	constexpr Position( int16_t x_, int16_t y_ ) : x{ x_ }, y{ y_ } {}
	constexpr Position( PositionParameter param ) : x{ param.x }, y{ param.y } {}

	int16_t x = 0;
	int16_t y = 0;
};

struct Color
{
	constexpr Color() = default;

	constexpr Color( uint8_t r_, uint8_t g_, uint8_t b_ ) : r{ r_ }, g{ g_ }, b{ b_ } {}

	explicit constexpr Color( uint32_t gpuParam )
		: r{ static_cast<uint8_t>( gpuParam ) }
		, g{ static_cast<uint8_t>( gpuParam >> 8 ) }
		, b{ static_cast<uint8_t>( gpuParam >> 16 ) }
	{}

	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
};

struct TexCoord
{
	constexpr TexCoord() = default;

	constexpr TexCoord( uint16_t u_, uint16_t v_ ) : u{ u_ }, v{ v_ } {}

	// tex coords are only 8bit in gpu param
	explicit constexpr TexCoord( uint32_t gpuParam )
		: u{ static_cast<uint16_t>( gpuParam & 0xff ) }
		, v{ static_cast<uint16_t>( ( gpuParam >> 8 ) & 0xff ) }
	{}

	// tex coords need to be larger than 8bit for rectangles
	uint16_t u = 0;
	uint16_t v = 0;
};

union ClutAttribute
{
	ClutAttribute() = default;
	ClutAttribute( uint16_t v ) : value{ static_cast<uint16_t>( v & 0x7fff ) } {}

	struct
	{
		uint16_t x : 6; // in half-word steps
		uint16_t y : 9; // in 1-line steps
		uint16_t : 1;
	};
	uint16_t value = 0;
};
static_assert( sizeof( ClutAttribute ) == 2 );

union TexPage
{
	static constexpr uint16_t WriteMask = 0x09ff;

	TexPage() = default;
	TexPage( uint16_t v ) : value{ static_cast<uint16_t>( v & WriteMask ) } {}

	struct
	{
		uint16_t texturePageBaseX : 4;
		uint16_t texturePageBaseY : 1;
		uint16_t semiTransparencymode : 2;
		uint16_t texturePageColors : 2;
		uint16_t : 2;
		uint16_t textureDisable : 1;
		uint16_t : 4;
	};
	uint16_t value = 0;
};
static_assert( sizeof( TexPage ) == 2 );

inline constexpr uint16_t TexPageWriteMask = 0x01ff;

struct Vertex
{
	Position position;
	Color color;
	TexCoord texCoord;
	ClutAttribute clut;
	TexPage texPage;
};

}