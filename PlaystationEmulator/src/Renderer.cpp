#include "Renderer.h"

#include "ClutShader.h"
#include "FullscreenShader.h"

#include <Render/Types.h>

#include <stdx/assert.h>
#include <stdx/bit.h>

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

	// TEMP
	SDL_SetWindowSize( window, VRamWidth, VRamHeight );

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
	m_originLoc = m_clutShader.GetUniformLocation( "u_origin" );
	m_displaySizeLoc = m_clutShader.GetUniformLocation( "u_displaySize" );

	m_texWindowMask = m_clutShader.GetUniformLocation( "u_texWindowMask" );
	m_texWindowOffset = m_clutShader.GetUniformLocation( "u_texWindowOffset" );

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
	(void)w;
	(void)h;

	/*
	dbLog( "Renderer::SetDisplaySize() -- %u, %u", w, h );
	if ( m_displayWidth != w || m_displayHeight != h )
	{
		m_displayWidth = w;
		m_displayHeight = h;
		SDL_SetWindowSize( m_window, static_cast<int>( w ), static_cast<int>( h ) );
	}
	*/
}

void Renderer::SetTextureWindow( uint32_t maskX, uint32_t maskY, uint32_t offsetX, uint32_t offsetY )
{
	dbLog( "Renderer::SetTextureWindow() -- mask: %u,%u offset: %u,%u", maskX, maskY, offsetX, offsetY );
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

		m_drawAreaLeft = left;
		m_drawAreaTop = top;
		m_drawAreaRight = right;
		m_drawAreaBottom = bottom;

		UpdateScissorRect();
	}
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

void Renderer::ReadVRam( uint16_t* vram )
{
	DrawBatch();

	glReadPixels( 0, 0, VRamWidth, VRamHeight, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, vram );
}

void Renderer::FillVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint16_t color )
{
	DrawBatch();

	auto toFloat = []( uint16_t c ) { return ( c & 31 ) / 31.0f; };

	const float red = toFloat( color );
	const float green = toFloat( color >> 5 );
	const float blue = toFloat( color >> 10 );
	const float alpha = ( color & 0x8000 ) ? 1.0f : 0.0f;

	glClearColor( red, green, blue, alpha );
	glScissor( left, top, width, height );
	glClear( GL_COLOR_BUFFER_BIT );

	glClearColor( 0, 0, 0, 1 );
	UpdateScissorRect();

	m_dirtyArea.Grow( Rect( left, top, left + width, top + height ) );
}

void Renderer::CopyVRam( GLint srcX, GLint srcY, GLint srcWidth, GLint srcHeight, GLint destX, GLint destY, GLint destWidth, GLint destHeight )
{
	DrawBatch();

	if ( m_dirtyArea.Intersects( Rect( srcX, srcX + srcWidth, srcY, srcY + srcHeight ) ) )
		UpdateReadTexture();

	m_vramReadFrameBuffer.Bind( Render::FrameBufferBinding::Draw );
	glDisable( GL_SCISSOR_TEST );

	glBlitFramebuffer(
		srcX, srcY, srcX + srcWidth, srcY + srcHeight,
		destX, destY, destX + destWidth, destY + destHeight,
		GL_COLOR_BUFFER_BIT, GL_NEAREST );

	m_vramDrawFrameBuffer.Bind( Render::FrameBufferBinding::Draw );
	glEnable( GL_SCISSOR_TEST );

	const auto x0 = std::clamp<int>( destX, 0, VRamWidth );
	const auto y0 = std::clamp<int>( destY, 0, VRamWidth );
	const auto x1 = std::clamp<int>( destX + destWidth, 0, VRamWidth );
	const auto y1 = std::clamp<int>( destY + destHeight, 0, VRamWidth );

	m_dirtyArea.Grow( x0, y0 );
	m_dirtyArea.Grow( x1, y1 );
}

void Renderer::CheckDrawMode( uint16_t drawMode, uint16_t clut )
{
	if ( m_lastDrawMode == drawMode && m_lastClut == clut )
		return; // cached values are the same

	if ( stdx::any_of<uint16_t>( drawMode, 1 << 11 ) )
		return; // textures are disabled

	const auto texBaseX = ( drawMode & 0xf ) * TexturePageBaseXMult;
	const auto texBaseY = ( ( drawMode >> 4 ) & 1 ) * TexturePageBaseYMult;
	const Rect texRect( texBaseX, texBaseY, TexturePageWidth, TexturePageHeight );

	const auto colorMode = ( drawMode >> 7 ) & 0x3;

	const auto clutBaseX = ( clut & 0x3f ) * ClutBaseXMult;
	const auto clutBaseY = ( ( clut >> 6 ) & 0x1ff ) * ClutBaseYMult;
	const Rect clutRect( clutBaseX, clutBaseY, ClutWidth, ClutHeight );

	if ( m_dirtyArea.Intersects( texRect ) || ( colorMode == 2 && m_dirtyArea.Intersects( clutRect ) ) )
		UpdateReadTexture();
}

void Renderer::UpdateScissorRect()
{
	const auto width = std::max<int>( m_drawAreaRight - m_drawAreaLeft + 1, 0 );
	const auto height = std::max<int>( m_drawAreaBottom - m_drawAreaTop + 1, 0 );
	glScissor( m_drawAreaLeft, m_drawAreaTop, width, height );
	dbCheckRenderErrors();
}

void Renderer::PushTriangle( const Vertex vertices[ 3 ] )
{
	if ( m_vertices.size() + 3 > VertexBufferSize )
		DrawBatch();

	// updates read texture if sampling from dirty area
	CheckDrawMode( vertices[ 0 ].drawMode, vertices[ 0 ].clut );

	// grow dirty area
	for ( size_t i = 0; i < 3; ++i )
	{
		auto& p = vertices[ i ].position;
		const auto x = std::clamp<int>( m_uniform.originX + p.x, m_drawAreaLeft, m_drawAreaRight );
		const auto y = std::clamp<int>( m_uniform.originY + p.y, m_drawAreaTop, m_drawAreaBottom );
		m_dirtyArea.Grow( x, y );
	}

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

	/*
	// grow dirty area to include polygons
	for ( auto& v : m_vertices )
	{
		const auto x = std::clamp<int>( v.position.x, m_drawAreaLeft, m_drawAreaRight );
		const auto y = std::clamp<int>( v.position.y, m_drawAreaTop, m_drawAreaBottom );
		m_dirtyArea.Grow( x, y );
	}
	*/

	m_vertexBuffer.SubData( m_vertices.size(), m_vertices.data() );
	glDrawArrays( GL_TRIANGLES, 0, m_vertices.size() );
	dbCheckRenderErrors();

	m_vertices.clear();

	// TEMP
	// UpdateReadTexture();
}

void Renderer::UpdateReadTexture()
{
	if ( m_dirtyArea.GetWidth() == 0 || m_dirtyArea.GetHeight() == 0 )
		return;

	m_vramReadFrameBuffer.Bind( Render::FrameBufferBinding::Draw );
	glDisable( GL_SCISSOR_TEST );

	glBlitFramebuffer( 
		m_dirtyArea.left, m_dirtyArea.top, m_dirtyArea.right, m_dirtyArea.bottom,
		m_dirtyArea.left, m_dirtyArea.top, m_dirtyArea.right, m_dirtyArea.bottom,
		GL_COLOR_BUFFER_BIT, GL_NEAREST );

	m_vramDrawFrameBuffer.Bind( Render::FrameBufferBinding::Draw );
	glEnable( GL_SCISSOR_TEST );

	ResetDirtyArea();
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
	m_vramDrawFrameBuffer.Unbind();
	m_noAttributeVAO.Bind();
	m_fullscreenShader.Bind();
	m_vramDrawTexture.Bind();
	dbCheckRenderErrors();

	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_BLEND );
	
	glClear( GL_COLOR_BUFFER_BIT );

	glViewport( 0, 0, VRamWidth, VRamHeight );

	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

	SDL_GL_SwapWindow( m_window );

	dbCheckRenderErrors();

	RestoreRenderState();
}

}