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

out vec4 BlendColor;
out vec2 TexCoord;
flat out ivec2 TexPageBase;
flat out ivec2 ClutBase;
flat out int DrawMode;

uniform vec2 origin;
uniform vec2 displaySize;

void main()
{
	// calculate normalized screen coordinate
	float x = ( 2.0 * ( v_pos.x + origin.x ) / displaySize.x ) - 1.0;
	float y = 1.0 - ( 2.0 * ( v_pos.y + origin.y ) / displaySize.y );
	gl_Position = vec4( x, y, 0.0, 1.0 );

	// calculate texture page offset
	TexPageBase = ivec2( ( v_drawMode & 0xf ) * 64, ( ( v_drawMode >> 4 ) & 0x1 ) * 256 );

	// calculate CLUT offset
	ClutBase = ivec2( ( v_clut & 0x3f ) * 16, v_clut >> 6 );

	BlendColor = vec4( v_color, 1.0 );
	TexCoord = v_texCoord;

	// send other texpage info to fragment shader
	DrawMode = v_drawMode;
}
)glsl";

const char* const ClutFragmentShader = R"glsl(
#version 330 core

in vec4 BlendColor;
in vec2 TexCoord;
flat in ivec2 TexPageBase;
flat in ivec2 ClutBase;
flat in int DrawMode;

out vec4 FragColor;

uniform ivec2 texWindowMask;
uniform ivec2 texWindowOffset;
uniform usampler2D vram;

// pos counted in halfwords steps
int SampleVRam( ivec2 pos )
{
	return int( texelFetch( vram, pos, 0 ).r );
}

int SampleClut( int index )
{
	return SampleVRam( ClutBase + ivec2( index, 0 ) );
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

float Convert5BitToFloat( int component )
{
	return float( component & 0x1f ) / 31.0;
}

vec4 ConvertABGR1555( int abgr )
{
	return vec4(
		Convert5BitToFloat( abgr ),
		Convert5BitToFloat( abgr >> 5 ),
		Convert5BitToFloat( abgr >> 10 ),
		1.0 ); // meaning of alpha bit depends on the transparency mode
}

void main()
{
	if ( bool( DrawMode & ( 1 << 11 ) ) )
	{
		// texture disabled
		FragColor = BlendColor;
		return;
	}

	// texCord counted in color depth mode
	ivec2 texCoord = ivec2( int( round( TexCoord.x ) ), int( round( TexCoord.y ) ) );

	// texCoord.x = ( ( texCoord.x & ~( texWindowMask.x * 8 ) ) | ( ( texWindowOffset.x & texWindowMask.x ) * 8 ) ) & 0xff;
	// texCoord.y = ( ( texCoord.y & ~( texWindowMask.y * 8 ) ) | ( ( texWindowOffset.y & texWindowMask.y ) * 8 ) ) & 0xff;

	int colorMode = ( DrawMode >> 7 ) & 0x3;

	int texel = 0;

	if ( colorMode == 0 )
	{
		texel = SampleClut( SampleIndex4( texCoord ) ); // get 4bit index
	}
	else if ( colorMode == 1 )
	{
		texel = SampleClut( SampleIndex8( texCoord ) ); // get 8bit index
	}
	else
	{
		texel = SampleVRam( TexPageBase + texCoord ); // get 16bit color directly
	}

	if ( texel == 0 )
		FragColor = vec4( 0.0 ); // all 0 is fully transparent
	else
		FragColor = ConvertABGR1555( texel ) * BlendColor * 2.0;
}
)glsl";

}