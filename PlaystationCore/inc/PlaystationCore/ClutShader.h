#pragma once

namespace PSX
{

const char* const ClutVertexShader = R"glsl(
#version 330 core

in vec2 v_pos;
in vec2 v_texCoord;
in vec3 v_color;
in int v_clut;
in int v_texPage;

out vec3 BlendColor;
out vec2 TexCoord;
out vec2 Position;
flat out ivec2 TexPageBase;
flat out ivec2 ClutBase;
flat out int TexPage;

void main()
{
	// calculate normalized screen coordinate
	float x = 2.0 * ( v_pos.x / 1024.0 ) - 1.0;
	float y = 2.0 * ( v_pos.y / 512.0 ) - 1.0;
	Position = v_pos;
	gl_Position = vec4( x, y, 0.0, 1.0 );

	// calculate texture page offset
	TexPageBase = ivec2( ( v_texPage & 0xf ) * 64, ( ( v_texPage >> 4 ) & 0x1 ) * 256 );

	// calculate CLUT offset
	ClutBase = ivec2( ( v_clut & 0x3f ) * 16, v_clut >> 6 );

	BlendColor = v_color;
	TexCoord = v_texCoord;

	// send other texpage info to fragment shader
	TexPage = v_texPage;
}
)glsl";

const char* const ClutFragmentShader = R"glsl(
#version 330 core

in vec3 BlendColor;
in vec2 TexCoord;
in vec2 Position;
flat in ivec2 TexPageBase;
flat in ivec2 ClutBase;
flat in int TexPage;

layout(location=0, index=0) out vec4 FragColor;
layout(location=0, index=1) out vec4 ParamColor;

uniform float u_srcBlend;
uniform float u_destBlend;
uniform bool u_setMaskBit;
uniform bool u_drawOpaquePixels;
uniform bool u_drawTransparentPixels;
uniform bool u_dither;
uniform bool u_realColor;
uniform ivec2 u_texWindowMask;
uniform ivec2 u_texWindowOffset;

uniform sampler2D u_vram;

vec3 FloorVec3( vec3 v )
{
	v.r = floor( v.r );
	v.g = floor( v.g );
	v.b = floor( v.b );
	return v;
}

vec3 RoundVec3( vec3 v )
{
	return FloorVec3( v + vec3( 0.5, 0.5, 0.5 ) );
}

uint mod_uint( uint x, uint y )
{
	return x - y * ( x / y );
}

int DitherTable[16] = int[16]( 4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2 );

vec3 Dither24bitTo15Bit( ivec2 pos, vec3 color )
{
	// assumes we are not using real color
	uint index = mod_uint( uint( pos.x ), 4u ) + mod_uint( uint( pos.y ), 4u ) * 4u;
	float offset = float( DitherTable[ index ] );
	color = FloorVec3( color ) + vec3( offset );
	color.r = clamp( color.r, 0.0, 255.0 );
	color.g = clamp( color.g, 0.0, 255.0 );
	color.b = clamp( color.b, 0.0, 255.0 );
	return FloorVec3( color / 8.0 );
}

vec3 ConvertColorTo15bit( vec3 color )
{
	color *= 31.0;
	if ( u_realColor )
		return color;
	else
		return RoundVec3( color );
}

vec3 ConvertColorTo24Bit( vec3 color )
{
	color *= 255.0;
	if ( u_realColor )
		return color;
	else
		return RoundVec3( color );
}

int FloatToInt5( float value )
{
	return int( floor( value * 31.0 + 0.5 ) );
}

int SampleVRam( ivec2 pos )
{
	vec4 c = texelFetch( u_vram, pos, 0 );
	int red = FloatToInt5( c.r );
	int green = FloatToInt5( c.g );
	int blue = FloatToInt5( c.b );
	int maskBit = int( ceil( c.a ) );
	return ( maskBit << 15 ) | ( blue << 10 ) | ( green << 5 ) | red;
}

vec4 SampleColor( ivec2 pos )
{
	vec4 color = texelFetch( u_vram, pos, 0 );
	color.rgb = ConvertColorTo15bit( color.rgb );
	color.a = ceil( color.a );
	return color;
}

vec4 SampleClut( int index )
{
	return SampleColor( ClutBase + ivec2( index, 0 ) );
}

// texCoord counted in 4bit steps
int SampleIndex4( ivec2 texCoord )
{
	int sample = SampleVRam( TexPageBase + ivec2( texCoord.x / 4, texCoord.y ) );
	int shiftAmount = ( texCoord.x & 0x3 ) * 4;
	return ( sample >> shiftAmount ) & 0xf;
}

// texCoord counted in 8bit steps
int SampleIndex8( ivec2 texCoord )
{
	int sample = SampleVRam( TexPageBase + ivec2( texCoord.x / 2, texCoord.y ) );
	int shiftAmount = ( texCoord.x & 0x1 ) * 8;
	return ( sample >> shiftAmount ) & 0xff;
}

vec4 LookupTexel()
{
	vec4 color;

	ivec2 texCoord = ivec2( int( floor( TexCoord.x + 0.5 ) ) & 0xff, int( floor( TexCoord.y + 0.5 ) ) & 0xff );

	texCoord.x = ( texCoord.x & ~( u_texWindowMask.x * 8 ) ) | ( ( u_texWindowOffset.x & u_texWindowMask.x ) * 8 );
	texCoord.y = ( texCoord.y & ~( u_texWindowMask.y * 8 ) ) | ( ( u_texWindowOffset.y & u_texWindowMask.y ) * 8 );

	int colorMode = ( TexPage >> 7 ) & 0x3;
	if ( colorMode == 0 )
	{
		color = SampleClut( SampleIndex4( texCoord ) ); // get 4bit index
	}
	else if ( colorMode == 1 )
	{
		color = SampleClut( SampleIndex8( texCoord ) ); // get 8bit index
	}
	else
	{
		color = SampleColor( TexPageBase + texCoord );
	}

	return color;
}

void main()
{
	vec4 color;

	float srcBlend = u_srcBlend;
	float destBlend = u_destBlend;

	vec3 blendColor = ConvertColorTo24Bit( BlendColor );

	if ( bool( TexPage & ( 1 << 11 ) ) )
	{
		// texture disabled
		color = vec4( blendColor, 0.0 );
	}
	else
	{
		// texture enabled
		color = LookupTexel();

		// check if pixel is fully transparent
		if ( color == vec4( 0.0 ) )
			discard;

		// blend color, result is 8bit
		color.rgb = ( color.rgb * blendColor.rgb ) / 16.0;

		if ( color.a == 0 )
		{
			if ( !u_drawOpaquePixels )
				discard;

			// disable semi transparency
			srcBlend = 1.0;
			destBlend = 0.0;
		}
		else if ( !u_drawTransparentPixels )
		{
			discard;
		}
	}

	if ( u_realColor )
	{
		color.rgb /= 255.0;
	}
	else if ( u_dither )
	{
		ivec2 pos = ivec2( int( floor( Position.x ) ), int( floor( Position.y ) ) );
		color.rgb = Dither24bitTo15Bit( pos, color.rgb ) / 31.0;
	}
	else
	{
		color.rgb = FloorVec3( color.rgb / 8.0 ) / 31.0;
	}

	if ( u_setMaskBit )
		color.a = 1.0;

	// output color
	FragColor = color;

	// use alpha for src blend, rgb for dest blend
	ParamColor = vec4( destBlend, destBlend, destBlend, srcBlend );

	// set depth buffer output
	gl_FragDepth = color.a;
}
)glsl";

}