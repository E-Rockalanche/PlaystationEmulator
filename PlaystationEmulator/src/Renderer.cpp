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
	float y = 1.0 - ( 2.0 * ( v_pos.y + origin.y ) / displaySize.y );
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

const char* FragmentShader = R"glsl(
#version 330 core

in vec4 BlendColor;
in vec2 TexCoord;
flat in ivec2 TexPageBase;
flat in ivec2 ClutBase;
flat in int DrawMode;

out vec4 FragColor;

uniform ivec2 texWindowMask;
uniform ivec2 texWindowOffset;
uniform usampler2D vram;

// pos counted in halfwords steps
int SampleVRam( ivec2 pos )
{
	return int( texelFetch( vram, pos, 0 ).r );
}

int SampleClut( int index )
{
	return SampleVRam( ClutBase + ivec2( index, 0 ) );
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

float Convert5BitToFloat( int component )
{
	return float( component & 0x1f ) / 31.0;
}

vec4 ConvertABGR1555( int abgr )
{
	return vec4(
		Convert5BitToFloat( abgr ),
		Convert5BitToFloat( abgr >> 5 ),
		Convert5BitToFloat( abgr >> 10 ),
		1.0 ); // meaning of alpha bit depends on the transparency mode
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

	int texel = 0;

	if ( colorMode == 0 )
	{
		texel = SampleClut( SampleIndex4( texCoord ) ); // get 4bit index
	}
	else if ( colorMode == 1 )
	{
		texel = SampleClut( SampleIndex8( texCoord ) ); // get 8bit index
	}
	else
	{
		texel = SampleVRam( TexPageBase + texCoord ); // get 16bit color directly
	}

	if ( texel == 0 )
		FragColor = vec4( 0.0 ); // all 0 is fully transparent
	else
		FragColor = ConvertABGR1555( texel ) * BlendColor * 2.0;
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
	m_shader.SetVertexAttribPointerInt( "v_drawMode", 1, Render::Type::UShort, Stride, offsetof( Vertex, Vertex::drawMode ) );
	
	// get shader uniform locations
	m_originLoc = m_shader.GetUniformLocation( "origin" );
	m_displaySizeLoc = m_shader.GetUniformLocation( "displaySize" );
	m_texWindowMask = m_shader.GetUniformLocation( "texWindowMask" );
	m_texWindowOffset = m_shader.GetUniformLocation( "texWindowOffset" );

	// set shader uniforms
	SetOrigin( 0, 0 );
	SetDisplaySize( 640, 480 );
	SetTextureWindow( 0, 0, 0, 0 );

	// texture only used to view VRAM easily
	m_vramColorTables = Render::Texture2D::Create( GL_RGB8, VRamWidth, VRamHeight, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, nullptr );
	m_vramColorTables.SetParamater( GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	m_vramColorTables.SetParamater( GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	m_vramColorTables.SetParamater( GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	m_vramColorTables.SetParamater( GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	// texture to sample in shader
	m_vramTextures = Render::Texture2D::Create( GL_R16UI, VRamWidth, VRamHeight, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr );
	m_vramTextures.SetParamater( GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	m_vramTextures.SetParamater( GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	m_vramTextures.SetParamater( GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	m_vramTextures.SetParamater( GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	return true;
}

void Renderer::UploadVRam( const uint16_t* vram )
{
	DrawBatch(); // draw pending polygons before updating vram with new textures/clut

	m_vramColorTables.SubImage( 0, 0, VRamWidth, VRamHeight, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, vram );

	m_vramTextures.SubImage( 0, 0, VRamWidth, VRamHeight, GL_RED_INTEGER, GL_UNSIGNED_SHORT, vram );
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

void Renderer::SetTextureWindow( uint32_t maskX, uint32_t maskY, uint32_t offsetX, uint32_t offsetY )
{
	dbLog( "Renderer::SetTextureWindow() -- mask: %u,%u offset: %u,%u" , maskX, maskY, offsetX, offsetY );
	if ( m_uniform.texWindowMaskX != maskX || m_uniform.texWindowMaskY != maskY || m_uniform.texWindowOffsetX != offsetX || m_uniform.texWindowOffsetY != offsetY )
	{
		DrawBatch();

		m_uniform.texWindowMaskX = maskX;
		m_uniform.texWindowMaskY = maskY;
		m_uniform.texWindowOffsetX = offsetX;
		m_uniform.texWindowOffsetY = offsetY;

		glUniform2i( m_texWindowMask, maskX, maskY );
		glUniform2i( m_texWindowOffset, offsetX, offsetY );
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

	m_vramTextures.Bind();

	m_vertexBuffer.SubData( 0, m_vertices.size() * sizeof( Vertex ), m_vertices.data() );
	glDrawArrays( GL_TRIANGLES, 0, m_vertices.size() );
	dbCheckRenderErrors();

	m_vertices.clear();
}

}