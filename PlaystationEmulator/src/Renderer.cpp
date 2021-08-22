#include "Renderer.h"

#include "ClutShader.h"
#include "FullscreenShader.h"

#include <stdx/assert.h>

#include <Render/Types.h>

#include <algorithm>

namespace PSX
{

namespace
{
constexpr size_t VertexBufferSize = 1024;
}

bool Renderer::Initialize( SDL_Window* window )
{
	dbExpects( window );
	m_window = window;

	// create no attribute VAO for fullscreen quad rendering
	m_noAttributeVAO = Render::VertexArrayObject::Create();

	// create VAO to attach attributes and shader to
	m_vramDrawVAO = Render::VertexArrayObject::Create();
	m_vramDrawVAO.Bind();

	// create vertex buffer
	m_vertexBuffer = Render::ArrayBuffer::Create();
	m_vertexBuffer.SetData<Vertex>( Render::BufferUsage::StreamDraw, VertexBufferSize );
	m_vertices.reserve( VertexBufferSize );
	
	// create shaders
	m_fullscreenShader = Render::Shader::Compile( FullscreenVertexShader, FullscreenFragmentShader );
	dbAssert( m_fullscreenShader.Valid() );

	m_clutShader = Render::Shader::Compile( ClutVertexShader, ClutFragmentShader );
	dbAssert( m_clutShader.Valid() );

	// set shader attribute locations in VAO
	constexpr auto Stride = sizeof( Vertex );

	m_clutShader.Bind();
	m_vramDrawVAO.AddFloatAttribute( m_clutShader.GetAttributeLocation( "v_pos" ), 2, Render::Type::Short, false, Stride, offsetof( Vertex, Vertex::position ) );
	m_vramDrawVAO.AddFloatAttribute( m_clutShader.GetAttributeLocation( "v_color" ), 3, Render::Type::UByte, true, Stride, offsetof( Vertex, Vertex::color ) );
	m_vramDrawVAO.AddFloatAttribute( m_clutShader.GetAttributeLocation( "v_texCoord" ), 2, Render::Type::UShort, false, Stride, offsetof( Vertex, Vertex::texCoord ) );
	m_vramDrawVAO.AddIntAttribute( m_clutShader.GetAttributeLocation( "v_clut" ), 1, Render::Type::UShort, Stride, offsetof( Vertex, Vertex::clut ) );
	m_vramDrawVAO.AddIntAttribute( m_clutShader.GetAttributeLocation( "v_drawMode" ), 1, Render::Type::UShort, Stride, offsetof( Vertex, Vertex::drawMode ) );
	
	// get shader uniform locations
	m_originLoc = m_clutShader.GetUniformLocation( "origin" );
	m_displaySizeLoc = m_clutShader.GetUniformLocation( "displaySize" );

	m_texWindowMask = m_clutShader.GetUniformLocation( "texWindowMask" );
	m_texWindowOffset = m_clutShader.GetUniformLocation( "texWindowOffset" );

	// m_vramColorTables = Render::Texture2D::Create( GL_RGB8, VRamWidth, VRamHeight, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, nullptr );
	// m_vramReadTexture = Render::Texture2D::Create( Render::InternalFormat::R16UI, VRamWidth, VRamHeight, Render::PixelFormat::Red_Int, Render::PixelType::UShort );

	// VRAM draw texture
	m_vramDrawTexture = Render::Texture2D::Create( Render::InternalFormat::RGBA8, VRamWidth, VRamHeight, Render::PixelFormat::RGBA, Render::PixelType::UByte );
	m_vramDrawFrameBuffer = Render::FrameBuffer::Create();
	m_vramDrawFrameBuffer.AttachTexture( Render::AttachmentType::Color, m_vramDrawTexture );
	dbAssert( m_vramDrawFrameBuffer.IsComplete() );
	m_vramDrawFrameBuffer.Unbind();

	// VRAM read texture
	m_vramReadTexture = Render::Texture2D::Create( Render::InternalFormat::RGBA8, VRamWidth, VRamHeight, Render::PixelFormat::RGBA, Render::PixelType::UByte );
	m_vramReadFrameBuffer = Render::FrameBuffer::Create();
	m_vramReadFrameBuffer.AttachTexture( Render::AttachmentType::Color, m_vramReadTexture );
	dbAssert( m_vramReadFrameBuffer.IsComplete() );
	m_vramReadFrameBuffer.Unbind();

	// set shader uniforms
	SetOrigin( 0, 0 );
	SetDisplaySize( 640, 480 );
	SetTextureWindow( 0, 0, 0, 0 );

	// get ready to render!
	RestoreRenderState();

	return true;
}

void Renderer::UpdateVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, const uint16_t* pixels )
{
	dbExpects( left + width <= VRamWidth );
	dbExpects( top + height <= VRamHeight );

	DrawBatch();

	m_vramDrawTexture.SubImage( left, top, width, height, Render::PixelFormat::RGBA, Render::PixelType::UShort_1_5_5_5_Rev, pixels );
	m_vramReadTexture.SubImage( left, top, width, height, Render::PixelFormat::RGBA, Render::PixelType::UShort_1_5_5_5_Rev, pixels );

	RestoreRenderState();
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
	// TEMP
	w = VRamWidth;
	h = VRamHeight;

	dbLog( "Renderer::SetDisplaySize() -- %u, %u", w, h );
	if ( m_displayWidth != w || m_displayHeight != h )
	{
		m_displayWidth = w;
		m_displayHeight = h;
		SDL_SetWindowSize( m_window, static_cast<int>( w ), static_cast<int>( h ) );
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

void Renderer::SetDrawArea( GLint left, GLint top, GLint right, GLint bottom )
{
	if ( m_drawAreaLeft != left || m_drawAreaTop != top || m_drawAreaRight != right || m_drawAreaBottom != bottom )
	{
		DrawBatch();

		const auto width = std::max( right - left + 1, 0 );
		const auto height = std::max( bottom - top + 1, 0 );
		glScissor( left, top, width, height );
		dbCheckRenderErrors();

		m_drawAreaLeft = left;
		m_drawAreaTop = top;
		m_drawAreaRight = right;
		m_drawAreaBottom = bottom;
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

	m_vertexBuffer.SubData( m_vertices.size(), m_vertices.data() );
	glDrawArrays( GL_TRIANGLES, 0, m_vertices.size() );
	dbCheckRenderErrors();

	m_vertices.clear();
}

void Renderer::RestoreRenderState()
{
	m_vramDrawVAO.Bind();
	m_vramDrawFrameBuffer.Bind();
	m_vramReadTexture.Bind();
	m_clutShader.Bind();
	dbCheckRenderErrors();

	glDisable( GL_CULL_FACE );

	glEnable( GL_SCISSOR_TEST );

	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	dbCheckRenderErrors();

	// restore uniforms
	// TODO: use uniform buffer
	glUniform2f( m_originLoc, static_cast<GLfloat>( m_uniform.originX ), static_cast<GLfloat>( m_uniform.originY ) );
	glUniform2f( m_displaySizeLoc, static_cast<GLfloat>( VRamWidth ), static_cast<GLfloat>( VRamHeight ) );
	dbCheckRenderErrors();

	glViewport( 0, 0, VRamWidth, VRamHeight );
}

void Renderer::DisplayFrame()
{
	m_noAttributeVAO.Bind();
	m_vramDrawFrameBuffer.Unbind();
	m_vramDrawTexture.Bind();
	m_fullscreenShader.Bind();
	dbCheckRenderErrors();

	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_BLEND );

	glViewport( 0, 0, m_displayWidth, m_displayHeight );

	// glClear( GL_COLOR_BUFFER_BIT );

	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	dbCheckRenderErrors();

	SDL_GL_SwapWindow( m_window );

	RestoreRenderState();
}

}