#include "VRamCopyShader.h"

namespace PSX
{

namespace
{

const char* const VertexShader = R"glsl(

#version 330 core

const vec2 s_positions[4] = vec2[]( vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0) );
const vec2 s_texCoords[4] = vec2[]( vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0) );

out vec2 TexCoord;

uniform vec4 u_srcRect; // 0-1

void main()
{
	TexCoord = u_srcRect.xy + u_srcRect.zw * s_texCoords[ gl_VertexID ];
	
	gl_Position = vec4( s_positions[ gl_VertexID ], 0.0, 1.0 );
}

)glsl";

const char* const FragmentShader = R"glsl(

#version 330 core

in vec2 TexCoord;

out vec4 FragColor;

uniform bool u_forceMaskBit;

uniform sampler2D u_vram;

void main()
{
	vec4 color = texture( u_vram, TexCoord );
	
	if ( u_forceMaskBit )
		color.a = 1.0;

	FragColor = color;

	// set depth from mask bit
	if ( color.a == 0.0 )
		gl_FragDepth = 1.0;
	else
		gl_FragDepth = -1.0;
}

)glsl";

} // namespace

void VRamCopyShader::Initialize()
{
	m_program = Render::Shader::Compile( VertexShader, FragmentShader );
	m_srcRectLoc = m_program.GetUniformLocation( "u_srcRect" );
	m_forceMaskBitLoc = m_program.GetUniformLocation( "u_forceMaskBit" );
}

void VRamCopyShader::Use(
	float srcX, float srcY, float srcW, float srcH,
	bool forceMaskBit )
{
	m_program.Bind();
	glUniform4f( m_srcRectLoc, srcX, srcY, srcW, srcH );
	glUniform1i( m_forceMaskBitLoc, forceMaskBit );
}

void VRamCopyShader::SetSourceArea( float srcX, float srcY, float srcW, float srcH )
{
	glUniform4f( m_srcRectLoc, srcX, srcY, srcW, srcH );
}

}