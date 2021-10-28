#pragma once

namespace PSX
{

const char* const ClutVertexShader = R"glsl(
#version 330 core

in vec2 v_pos;
in vec2 v_texCoord;
in vec3 v_color;
in int v_clut;
in int v_drawMode;

out vec3 BlendColor;
out vec2 TexCoord;
flat out ivec2 TexPageBase;
flat out ivec2 ClutBase;
flat out int DrawMode;

uniform vec2 u_origin;

void main()
{
	// calculate normalized screen coordinate
	float x = ( 2.0 * ( v_pos.x + u_origin.x ) / 1024 ) - 1.0;
	float y = ( 2.0 * ( v_pos.y + u_origin.y ) / 512 ) - 1.0;
	gl_Position = vec4( x, y, 0.0, 1.0 );

	// calculate texture page offset
	TexPageBase = ivec2( ( v_drawMode & 0xf ) * 64, ( ( v_drawMode >> 4 ) & 0x1 ) * 256 );

	// calculate CLUT offset
	ClutBase = ivec2( ( v_clut & 0x3f ) * 16, v_clut >> 6 );

	BlendColor = v_color;
	TexCoord = v_texCoord;

	// send other texpage info to fragment shader
	DrawMode = v_drawMode;
}
)glsl";

const char* const ClutFragmentShader = R"glsl(
#version 330 core

in vec3 BlendColor;
in vec2 TexCoord;
flat in ivec2 TexPageBase;
flat in ivec2 ClutBase;
flat in int DrawMode;

layout(location=0, index=0) out vec4 FragColor;
layout(location=0, index=1) out vec4 ParamColor;

uniform float u_srcBlend;
uniform float u_destBlend;
uniform bool u_setMaskBit;
uniform ivec2 u_texWindowMask;
uniform ivec2 u_texWindowOffset;
uniform sampler2D u_vram;

int FloatTo5bit( float value )
{
	return int( round( value * 31.0 ) );
}

int SampleVRam( ivec2 pos )
{
	vec4 c = texelFetch( u_vram, pos, 0 );
	int red = FloatTo5bit( c.r );
	int green = FloatTo5bit( c.g );
	int blue = FloatTo5bit( c.b );
	int maskBit = int( ceil( c.a ) );
	return ( maskBit << 15 ) | ( blue << 10 ) | ( green << 5 ) | red;
}

vec4 SampleClut( int index )
{
	return texelFetch( u_vram, ClutBase + ivec2( index, 0 ), 0 );
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

	ivec2 texCoord = ivec2( int( round( TexCoord.x ) ) & 0xff, int( round( TexCoord.y ) ) & 0xff );

	texCoord.x = ( texCoord.x & ~( u_texWindowMask.x * 8 ) ) | ( ( u_texWindowOffset.x & u_texWindowMask.x ) * 8 );
	texCoord.y = ( texCoord.y & ~( u_texWindowMask.y * 8 ) ) | ( ( u_texWindowOffset.y & u_texWindowMask.y ) * 8 );

	int colorMode = ( DrawMode >> 7 ) & 0x3;
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
		color = texelFetch( u_vram, TexPageBase + texCoord, 0 ); // get 16bit color directly
		color.a = ceil( color.a );
	}

	return color;
}

void main()
{
	vec4 color;

	float srcBlend = u_srcBlend;
	float destBlend = u_destBlend;

	if ( bool( DrawMode & ( 1 << 11 ) ) )
	{
		// texture disabled
		color = vec4( BlendColor, 0.0 );
	}
	else
	{
		// texture enabled
		color = LookupTexel();

		// check if pixel is fully transparent
		// TODO: can fully transparent pixels still set bit15 with setMask on?
		if ( color == vec4( 0.0 ) )
			discard;

		color.rgb *= BlendColor.rgb * 2.0;

		if ( color.a == 0 )
		{
			// disable semi transparency
			srcBlend = 1.0;
			destBlend = 0.0;
		}
	}

	if ( u_setMaskBit )
		color.a = 1.0;

	// output color
	FragColor = color;

	// use alpha for src blend, rgb for dest blend
	ParamColor = vec4( destBlend, destBlend, destBlend, srcBlend );
}
)glsl";

}