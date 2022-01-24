#pragma once

namespace PSX
{

const char* const DisplayVertexShader = R"glsl(
#version 330 core

const vec2 positions[4] = vec2[]( vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0) );
const vec2 texCoords[4] = vec2[]( vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0) );

out vec2 TexCoord;

void main()
{
	TexCoord = texCoords[ gl_VertexID ];
	gl_Position = vec4( positions[ gl_VertexID ], 0.0, 1.0 );
}

)glsl";

const char* const DisplayFragmentShader = R"glsl(
#version 330 core

in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D tex;

void main()
{
	FragColor = texture( tex, TexCoord );
}

)glsl";

}