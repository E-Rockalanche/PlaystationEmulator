#pragma once

namespace PSX
{

const char* const Output24bitVertexShader = R"glsl(

#version 330 core

const vec2 s_positions[4] = vec2[]( vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0) );
const vec2 s_texCoords[4] = vec2[]( vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.0), vec2(1.0, 0.0) );

out vec2 TexCoord;

void main()
{
	TexCoord = s_texCoords[ gl_VertexID ];
	gl_Position = vec4( s_positions[ gl_VertexID ], 0.0, 1.0 );
}

)glsl";

const char* const Output24bitFragmentShader = R"glsl(

#version 330 core

in vec2 TexCoord;

out vec4 FragColor;

uniform ivec2 u_destPos
uniform ivec2 u_destSize;
uniform sampler2D u_vram;

int FloatTo5bit( float value )
{
	return int( round( value * 31.0 ) );
}

int SampleVRam16( ivec2 pos )
{
	vec4 c = texelFetch( u_vram, pos, 0 );
	int red = FloatTo5bit( c.r );
	int green = FloatTo5bit( c.g );
	int blue = FloatTo5bit( c.b );
	int maskBit = int( ceil( c.a ) );
	return ( maskBit << 15 ) | ( blue << 10 ) | ( green << 5 ) | red;
}

vec3 SampleVRam24( ivec2 pos )
{
	int x = ( texCoord.x * 3 ) / 2;

	int sample1 = SampleVRam16( x, pos.y );
	int sample2 = SampleVRam16( x + 1, pos.y );

	int bgr = ( sample1 << 16 ) | sample2;

	if ( pos.x & 1 == 0 )
		bgr = bgr >> 8;

	float red = ( bgr & 0xff ) / 255.0;
	float green = ( ( bgr >> 8 ) & 0xff ) / 255.0;
	float blue = ( ( bgr >> 16 ) & 0xff ) / 255.0;

	return vec3( red, green, blue );
}

void main()
{
	ivec2 texCoord = u_destPos + ivec2( TexCoord * u_destSize );

	FragColor = vec4( SampleVRam24( texCoord ).rgb, 1.0 );
}

)glsl";

}