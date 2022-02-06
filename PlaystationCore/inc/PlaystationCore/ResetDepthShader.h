#pragma once

namespace PSX
{

const char* const ResetDepthVertexShader = R"glsl(

#version 330 core

const vec2 s_positions[4] = vec2[]( vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0) );
const vec2 s_texCoords[4] = vec2[]( vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0) );

out vec2 TexCoord;

void main()
{
	TexCoord = s_texCoords[ gl_VertexID ];
	
	gl_Position = vec4( s_positions[ gl_VertexID ], 0.0, 1.0 );
}

)glsl";

const char* const ResetDepthFragmentShader = R"glsl(

#version 330 core

in vec2 TexCoord;

uniform sampler2D u_vram;

void main()
{
	vec4 color = texture( u_vram, TexCoord );

	// set depth from mask bit
	gl_FragDepth = 1.0 - color.a;
}

)glsl";

}