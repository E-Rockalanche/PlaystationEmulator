#include "Renderer.h"

#include <Render/Types.h>

namespace PSX
{

namespace
{

const char* VertexShader = R"glsl(
#version 330 core
in vec2 pos;
in vec3 color;

out vec4 vertexColor;

uniform vec2 origin;
uniform vec2 displaySize;
uniform float alpha;

void main()
{
	float x = ( 2.0 * ( pos.x + origin.x ) / displaySize.x ) - 1.0;
	float y = 1.0 - ( 2.0 * ( pos.y + origin.y ) / displaySize.y );

	gl_Position = vec4( x, y, 0.0, 1.0 );

	vertexColor = vec4( color, alpha );
}
)glsl";

const char* FragmentShader = R"glsl(
#version 330 core
out vec4 FragColor;
in vec4 vertexColor;

void main()
{
	FragColor = vertexColor;
}
)glsl";

constexpr size_t VertexBufferSize = 3;

}

Renderer::Renderer()
{
	m_vao = Render::VertexArrayObject::Create();
	m_vao.Bind();

	m_vbo = Render::ArrayBuffer::Create();
	m_vbo.SetData( VertexBufferSize * sizeof( Vertex ), nullptr, Render::BufferUsage::StreamDraw );
	m_vertices.reserve( VertexBufferSize );

	m_shader = Render::Shader::Compile( VertexShader, FragmentShader );
	m_shader.Use();
	m_shader.SetVertexAttribPointer( "pos", 2, Render::GetTypeEnum<decltype( Position::x )>(), false, sizeof( Vertex ), 0 );
	m_shader.SetVertexAttribPointer( "color", 3, Render::GetTypeEnum<decltype( Color::r )>(), true, sizeof( Vertex ), offsetof( Vertex, Vertex::color ) );
	
	m_originLoc = m_shader.GetUniformLocation( "origin" );
	m_displaySizeLoc = m_shader.GetUniformLocation( "displaySize" );
	m_alphaLoc = m_shader.GetUniformLocation( "alpha" );

	SetOrigin( 0, 0 );
	SetDisplaySize( 256, 224 );
	SetAlpha( 1.0f );
}

void Renderer::SetOrigin( int16_t x, int16_t y )
{
	std::cout << "origin: " << x << ", " << y << std::endl;
	glUniform2f( m_originLoc, x, y );
}

void Renderer::SetDisplaySize( uint16_t w, uint16_t h )
{
	std::cout << "display size: " << w << ", " << h << std::endl;
	glUniform2f( m_displaySizeLoc, w, h );
}

void Renderer::SetAlpha( float alpha )
{
	std::cout << "alpha: " << alpha << std::endl;
	glUniform1f( m_alphaLoc, alpha );
}

void Renderer::PushTriangle( const Vertex& v1, const Vertex v2, const Vertex& v3 )
{
	if ( m_vertices.size() + 3 > VertexBufferSize )
		DrawBatch();

	m_vertices.push_back( v1 );
	m_vertices.push_back( v2 );
	m_vertices.push_back( v3 );
}

void Renderer::DrawBatch()
{
	m_vbo.SubData( 0, m_vertices.size() * sizeof( Vertex ), m_vertices.data() );
	glDrawArrays( GL_TRIANGLES, 0, m_vertices.size() );
	m_vertices.clear();
}

}