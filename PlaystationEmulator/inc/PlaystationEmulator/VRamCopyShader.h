#pragma once

namespace PSX
{

const char* const VRamCopyVertexShader = R"glsl(

#version 330 core

const vec2 s_positions[4] = vec2[]( vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0) );
const vec2 s_texCoords[4] = vec2[]( vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0) );

out vec2 TexCoord;

uniform ivec4 u_srcRect;
uniform ivec4 u_destRect;

void main()
{
	TexCoord = vec2( u_srcRect.xy ) + s_texCoords[ gl_VertexID ] * vec2( u_srcRect.zw );

	vec2 pos = vec2( u_destRect.xy ) + s_positions[ gl_VertexID ] * vec2( u_destRect.zw );
	gl_Position = vec4( pos.xy, 0.0, 1.0 );
}

)glsl";

const char* const VRamCopyFragmentShader = R"glsl(

#version 330 core

in vec2 TexCoord;

out vec4 FragColor;

uniform bool u_setMaskBit;

uniform sampler2D u_vram;

void main()
{
	vec4 color = texture( u_vram, TexCoord );
	
	if ( u_setMaskBit )
		color.a = 1.0;

	FragColor = color;
	gl_FragDepth = color.a;
}

)glsl";

}