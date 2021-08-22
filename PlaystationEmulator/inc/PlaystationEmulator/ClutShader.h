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
	float y = ( 2.0 * ( v_pos.y + origin.y ) / displaySize.y ) - 1.0;
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
uniform sampler2D vram;

int FloatTo5bit( float value )
{
	return int( round( value * 31 ) );
}

int SampleVRam( ivec2 pos )
{
	vec4 c = texelFetch( vram, pos, 0 );
	int red = FloatTo5bit( c.r );
	int green = FloatTo5bit( c.g );
	int blue = FloatTo5bit( c.b );
	int maskBit = int( round( c.a ) );
	return ( maskBit << 15 ) | ( blue << 10 ) | ( green << 5 ) | red;
}

vec4 SampleClut( int index )
{
	return texelFetch( vram, ClutBase + ivec2( index, 0 ), 0 );
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

	if ( colorMode == 0 )
	{
		FragColor = SampleClut( SampleIndex4( texCoord ) ); // get 4bit index
	}
	else if ( colorMode == 1 )
	{
		FragColor = SampleClut( SampleIndex8( texCoord ) ); // get 8bit index
	}
	else
	{
		FragColor = texelFetch( vram, TexPageBase + texCoord, 0 ); // get 16bit color directly
	}

	// only all 0 is fully transparent
	if ( FragColor != vec4( 0.0 ) )
	{
		FragColor *= BlendColor * 2.0;
		FragColor.a = 1.0; // TODO: blend modes
	}
}
)glsl";

}