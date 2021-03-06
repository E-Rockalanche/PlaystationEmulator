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

uniform ivec4 u_srcRect;

uniform sampler2D u_vram;

uint FloatTo5bit( float value )
{
	return uint( round( value * 31.0 ) );
}

uint SampleVRam16( ivec2 pos )
{
	vec4 c = texture( u_vram, pos / vec2( 1024.0, 512.0 ) );
	uint red = FloatTo5bit( c.r );
	uint green = FloatTo5bit( c.g );
	uint blue = FloatTo5bit( c.b );
	uint maskBit = uint( ceil( c.a ) );
	return ( maskBit << 15 ) | ( blue << 10 ) | ( green << 5 ) | red;
}

vec3 SampleVRam24( ivec2 base, ivec2 offset )
{
	int x = base.x + ( offset.x * 3 ) / 2;
	int y = base.y + offset.y;

	uint sample1 = SampleVRam16( ivec2( x, y ) );
	uint sample2 = SampleVRam16( ivec2( x + 1, y ) );

	uint r, g, b;
	if ( ( offset.x & 1 ) == 0 )
	{
		r = sample1 & 0xffu;
		g = sample1 >> 8;
		b = sample2 & 0xffu;
	}
	else
	{
		r = sample1 >> 8;
		g = sample2 & 0xffu;
		b = sample2 >> 8;
	}

	return vec3( float( r ) / 255.0, float( g ) / 255.0, float( b ) / 255.0 );
}

void main()
{
	ivec2 base =  u_srcRect.xy;
	ivec2 offset = ivec2( TexCoord * u_srcRect.zw );
	vec3 sample = SampleVRam24( base, offset );
	FragColor = vec4( sample.rgb, 1.0 );
}

)glsl";

}