#include "Renderer.h"

#include <stdx/assert.h>

#include <Render/Types.h>

namespace PSX
{

namespace
{

const char* VertexShader = R"glsl(
#version 330 core

in vec2 v_pos;
in vec3 v_color;
in vec2 v_texCoord;
in int v_clut;
in int v_texPage;

out vec4 BlendColor;
out vec2 TexCoord;
flat out ivec2 TexPageBase;
flat out ivec2 ClutBase;
flat out int TexPage;

uniform vec2 origin;
uniform vec2 displaySize;

void main()
{
	// calculate normalized screen coordinate
	float x = ( 2.0 * ( v_pos.x + origin.x ) / displaySize.x ) - 1.0;
	float y = 1.0 - ( 2.0 * ( v_pos.y + origin.y ) / displaySize.y );
	gl_Position = vec4( x, y, 0.0, 1.0 );

	// calculate texture page ofset (in byte coordinates, not halfwords)
	TexPageBase ivec2( ( v_texPage & 0xf ) * 64 * 2, ( ( v_texPage >> 4 ) & 0x1 ) * 256 );

	// calculate CLUT offset in 16bit pixel coordinates
	ivec2 clutBase = ivec2( ( v_clut & 0x3f ) * 16, v_clut >> 6 );

	BlendColor = vec4( v_color, 1.0 );
	TexCoord = v_texCoord;
	TexPage = v_texPage;
	ClutBase = clutBase;
}
)glsl";

const char* FragmentShader = R"glsl(
#version 330 core

in vec4 BlendColor;
in vec2 TexCoord;
flat in ivec2 TexPageBase;
flat in ivec2 ClutBase;
flat in int TexPage;

out vec4 FragColor;

uniform sampler2D vramClut;
uniform sampler2D vramTex;

int SampleVRamByte( ivec2 pos )
{
	return int( texelFetch( vramTex, pos, 0 ).r * 255.0f );
}

float Convert5BitToFloat( int component )
{
	return float( component & 0x1f ) / 31.0;
}

vec4 ConvertRGB5551( int rgba )
{
	return vec4(
		Convert5BitToFloat( rgba ),
		Convert5BitToFloat( rgba >> 5 ),
		Convert5BitToFloat( rgba >> 10 ),
		( rgba >> 15 ) * 1.0 );
}

void main()
{
	FragColor = BlendColor;

	if ( bool( TexPage & ( 1 << 11 ) ) )
	{
		FragColor = BlendColor;
		return;
	}

	ivec2 texCoord = ivec2( TexCoord );

	int colorDepth = ( TexPage >> 7 ) & 0x3;

	if ( colorDepth == 0 )
	{
		// get 4bit index
		int byte = SampleVRamByte( TexPageBase + ivec2( texCoord.x / 2, texCoord.y ) );
		int shift = ( texCoord.x & 0x1 ) * 4;
		int clutIndex = ( byte >> shift ) & 0xf;
		vec4 texel = texelFetch( vramClut, ClutBase + ivec2( clutIndex, 0 ), 0 );
		FragColor = texel * BlendColor * 2.0;
	}
	else if ( colorDepth == 1 )
	{
		// get 8bit index
		int clutIndex = SampleVRamByte( TexPageBase + texCoord );
		vec4 texel = texelFetch( vramClut, ClutBase + ivec2( clutIndex, 0 ), 0 );
		FragColor = texel * BlendColor * 2.0;
	}
	else
	{
		// get 16bit color
		int low = SampleVRamByte( TexPageBase + ivec2( texCoord.x * 2, texCoord.y ) );
		int high = SampleVRamByte( TexPageBase + ivec2( texCoord.x * 2 + 1, texCoord.y ) );
		int rgba5551 = low | ( high << 8 );
		FragColor = ConvertRGB5551( rgba5551 ) * BlendColor * 2.0;
	}
}
)glsl";

constexpr size_t VertexBufferSize = 1024;

}

bool Renderer::Initialize( SDL_Window* window )
{
	dbExpects( window );
	m_window = window;

	// create VAO to attach attributes and shader to
	m_vao = Render::VertexArrayObject::Create();
	m_vao.Bind();

	// create vertex buffer
	m_vertexBuffer = Render::ArrayBuffer::Create();
	m_vertexBuffer.SetData( Render::BufferUsage::StreamDraw, VertexBufferSize * sizeof( Vertex ) );
	m_vertices.reserve( VertexBufferSize );

	// create shader
	m_shader = Render::Shader::Compile( VertexShader, FragmentShader );
	if ( !m_shader.Valid() )
	{
		dbBreak();
		return false;
	}

	m_shader.Use();

	// set shader attribute locations in VAO
	constexpr auto Stride = sizeof( Vertex );
	m_shader.SetVertexAttribPointer( "v_pos", 2, Render::Type::Short, false, Stride, offsetof( Vertex, Vertex::position ) );
	m_shader.SetVertexAttribPointer( "v_color", 3, Render::Type::UByte, true, Stride, offsetof( Vertex, Vertex::color ) );
	m_shader.SetVertexAttribPointer( "v_texCoord", 2, Render::Type::UShort, false, Stride, offsetof( Vertex, Vertex::texCoord ) );
	m_shader.SetVertexAttribPointerInt( "v_clut", 1, Render::Type::UShort, Stride, offsetof( Vertex, Vertex::clut ) );
	m_shader.SetVertexAttribPointerInt( "v_texPage", 1, Render::Type::UShort, Stride, offsetof( Vertex, Vertex::texPage ) );
	
	// get shader uniform locations
	m_originLoc = m_shader.GetUniformLocation( "origin" );
	m_displaySizeLoc = m_shader.GetUniformLocation( "displaySize" );

	// set shader uniforms
	SetOrigin( 0, 0 );
	SetDisplaySize( 640, 480 );

	// create VRAM textures
	glActiveTexture( GL_TEXTURE0 );
	m_vramColorTables = Render::Texture2D::Create( GL_RGB8, VRamWidth16, VRamHeight, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, nullptr );
	m_vramColorTables.SetParamater( GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	m_vramColorTables.SetParamater( GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	m_vramColorTables.SetParamater( GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	m_vramColorTables.SetParamater( GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	glActiveTexture( GL_TEXTURE1 );
	m_vramTextures = Render::Texture2D::Create( GL_R8, VRamWidth8, VRamHeight, GL_RED, GL_UNSIGNED_BYTE, nullptr );
	m_vramTextures.SetParamater( GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	m_vramTextures.SetParamater( GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	m_vramTextures.SetParamater( GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	m_vramTextures.SetParamater( GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	return true;
}

void Renderer::UploadVRam( const uint16_t* vram )
{
	DrawBatch(); // draw pending polygons before updating vram with new textures/clut

	m_vramColorTables.SubImage( 0, 0, VRamWidth16, VRamHeight, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, vram );

	m_vramTextures.SubImage( 0, 0, VRamWidth8, VRamHeight, GL_RED, GL_UNSIGNED_BYTE, vram );
}

void Renderer::SetOrigin( int32_t x, int32_t y )
{
	dbLog( "Renderer::SetOrigin() -- %i, %i", x, y );
	if ( m_uniform.originX != x || m_uniform.originY != y )
	{
		DrawBatch();

		m_uniform.originX = x;
		m_uniform.originY = y;

		glUniform2f( m_originLoc, static_cast<GLfloat>( x ), static_cast<GLfloat>( y ) );
		dbCheckRenderErrors();
	}
}

void Renderer::SetDisplaySize( uint32_t w, uint32_t h )
{
	dbLog( "Renderer::SetDisplaySize() -- %u, %u", w, h );
	if ( m_uniform.displayWidth != w || m_uniform.displayHeight != h )
	{
		DrawBatch();

		m_uniform.displayWidth = w;
		m_uniform.displayHeight = h;

		SDL_SetWindowSize( m_window, static_cast<int>( w ), static_cast<int>( h ) );
		glViewport( 0, 0, w, h );
		glUniform2f( m_displaySizeLoc, static_cast<GLfloat>( w ), static_cast<GLfloat>( h ) );
		dbCheckRenderErrors();
	}
}

void Renderer::PushTriangle( const Vertex vertices[ 3 ] )
{
	if ( m_vertices.size() + 3 > VertexBufferSize )
		DrawBatch();

	m_vertices.insert( m_vertices.end(), vertices, vertices + 3 );
}

void Renderer::PushQuad( const Vertex vertices[ 4 ] )
{
	PushTriangle( vertices );
	PushTriangle( vertices + 1 );
}

void Renderer::DrawBatch()
{
	if ( m_vertices.empty() )
		return;
	
	m_vao.Bind();
	m_vertexBuffer.Bind();
	m_shader.Use();

	glActiveTexture( GL_TEXTURE0 );
	m_vramColorTables.Bind();

	glActiveTexture( GL_TEXTURE1 );
	m_vramTextures.Bind();

	m_vertexBuffer.SubData( 0, m_vertices.size() * sizeof( Vertex ), m_vertices.data() );
	glDrawArrays( GL_TRIANGLES, 0, m_vertices.size() );
	dbCheckRenderErrors();

	m_vertices.clear();
}

}