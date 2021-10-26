#pragma once

namespace PSX
{

const char* const Output16bitVertexShader = R"glsl(

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

const char* const Output16bitFragmentShader = R"glsl(

#version 330 core

in vec2 TexCoord;

out vec4 FragColor;

uniform ivec2 u_srcPos;
uniform ivec2 u_srcSize;
uniform sampler2D u_vram;

void main()
{
	ivec2 texCoord = u_srcPos + ivec2( TexCoord * u_srcSize );

	FragColor = vec4( texelFetch( u_vram, texCoord, 0 ).rgb, 1.0 );
}

)glsl";

}