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
	m_alphaLoc = m_clutShader.GetUniformLocation( "u_alpha" );
	m_semiTransparentLoc = m_clutShader.GetUniformLocation( "u_semiTransparent" );
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

	// initialize shader uniforms
	glUniform2f( m_originLoc, 0.0f, 0.0f );
	glUniform2i( m_texWindowMask, 0, 0 );
	glUniform2i( m_texWindowOffset, 0, 0 );
	glScissor( 0, 0, 0, 0 );

	// get ready to render!
	RestoreRenderState();

	return true;
}

void Renderer::Reset()
{
	// TODO: clear vram

	// GPU will reset uniforms

	m_vertices.clear();
	ResetDirtyArea();
	m_renderedPrimitive = false;
}

void Renderer::EnableVRamView( bool enable )
{
	if ( !m_viewVRam && enable )
		SDL_SetWindowSize( m_window, VRamWidth, VRamHeight );

	if ( m_viewVRam && !enable )
		SDL_SetWindowSize( m_window, m_displayWidth, m_displayHeight );

	m_viewVRam = enable;
}

void Renderer::SetOrigin( int32_t x, int32_t y )
{
	if ( m_uniform.originX != x || m_uniform.originY != y )
	{
		DrawBatch();

		m_uniform.originX = x;
		m_uniform.originY = y;

		glUniform2f( m_originLoc, static_cast<GLfloat>( x ), static_cast<GLfloat>( y ) );
		dbCheckRenderErrors();
	}
}

void Renderer::SetDisplayStart( uint32_t x, uint32_t y )
{
	m_displayX = x;
	m_displayY = y;
}

void Renderer::SetDisplaySize( uint32_t w, uint32_t h )
{
	if ( m_displayWidth != w || m_displayHeight != h )
	{
		m_displayWidth = w;
		m_displayHeight = h;

		if ( !m_viewVRam && w > 0 && h > 0 )
			SDL_SetWindowSize( m_window, w, h );
	}
}

void Renderer::SetTextureWindow( uint32_t maskX, uint32_t maskY, uint32_t offsetX, uint32_t offsetY )
{
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

void Renderer::SetSemiTransparency( SemiTransparency semiTransparency )
{
	if ( m_semiTransparency != semiTransparency )
	{
		if ( m_uniform.semiTransparent )
			DrawBatch();

		m_semiTransparency = semiTransparency;
		UpdateBlendMode();
	}
}

void Renderer::SetMaskBits( bool setMask, bool checkMask )
{
	// TODO: stencil
	(void)setMask;
	(void)checkMask;
}

void Renderer::SetSemiTransparencyEnabled( bool enabled )
{
	if ( m_uniform.semiTransparent != enabled )
	{
		DrawBatch();
		m_uniform.semiTransparent = enabled;
		glUniform1i( m_semiTransparentLoc, enabled );
		UpdateBlendMode();
	}
}

void Renderer::UpdateVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, const uint16_t* pixels )
{
	dbExpects( left + width <= VRamWidth );
	dbExpects( top + height <= VRamHeight );

	// TODO: check draw mode areas
	DrawBatch();

	m_vramDrawTexture.SubImage( left, top, width, height, Render::PixelFormat::RGBA, Render::PixelType::UShort_1_5_5_5_Rev, pixels );
	m_vramReadTexture.SubImage( left, top, width, height, Render::PixelFormat::RGBA, Render::PixelType::UShort_1_5_5_5_Rev, pixels );
}

void Renderer::ReadVRam( uint16_t* vram )
{
	// TODO: check dirty area
	DrawBatch();

	glReadPixels( 0, 0, VRamWidth, VRamHeight, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, vram );
}

void Renderer::FillVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint16_t color )
{
	// TODO: check draw mode areas
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
	// TODO: check draw mode areas
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

	m_dirtyArea.Grow( destX, destY );
	m_dirtyArea.Grow( destX + destWidth, destY + destHeight );
}

void Renderer::CheckDrawMode( uint16_t drawMode, uint16_t clut )
{
	if ( m_lastDrawMode == drawMode && m_lastClut == clut )
		return; // cached values are the same

	m_lastDrawMode = drawMode;
	m_lastClut = clut;

	// 5-6   Semi Transparency     (0=B/2+F/2, 1=B+F, 2=B-F, 3=B+F/4)   ;GPUSTAT.5-6
	SetSemiTransparency( static_cast<SemiTransparency>( ( drawMode >> 5 ) & 0x3 ) );

	if ( stdx::any_of<uint16_t>( drawMode, 1 << 11 ) )
		return; // textures are disabled

	const auto colorMode = ( drawMode >> 7 ) & 0x3;
	dbAssert( colorMode < 3 );

	const int texBaseX = ( drawMode & 0xf ) * TexturePageBaseXMult;
	const int texBaseY = ( ( drawMode >> 4 ) & 1 ) * TexturePageBaseYMult;
	const int texSize = 64 << colorMode;
	const Rect texRect( texBaseX, texBaseY, texSize, texSize );

	if ( m_dirtyArea.Intersects( texRect ) )
	{
		UpdateReadTexture();
	}
	else if ( colorMode < 2 )
	{
		const int clutBaseX = ( clut & 0x3f ) * ClutBaseXMult;
		const int clutBaseY = ( ( clut >> 6 ) & 0x1ff ) * ClutBaseYMult;
		const Rect clutRect( clutBaseX, clutBaseY, 32 << colorMode, ClutHeight );

		if ( m_dirtyArea.Intersects( clutRect ) )
			UpdateReadTexture();
	}
}

void Renderer::UpdateScissorRect()
{
	const auto width = std::max<int>( m_drawAreaRight - m_drawAreaLeft + 1, 0 );
	const auto height = std::max<int>( m_drawAreaBottom - m_drawAreaTop + 1, 0 );
	glScissor( m_drawAreaLeft, m_drawAreaTop, width, height );
	dbCheckRenderErrors();
}

void Renderer::UpdateBlendMode()
{
	if ( m_uniform.semiTransparent )
	{
		glEnable( GL_BLEND );
		switch ( m_semiTransparency )
		{
			case SemiTransparency::Blend:
				glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
				glBlendEquation( GL_FUNC_ADD );
				glUniform1f( m_alphaLoc, 0.5f );
				break;

			case SemiTransparency::Add:
				glBlendFunc( GL_ONE, GL_ONE );
				glBlendEquation( GL_FUNC_ADD );
				glUniform1f( m_alphaLoc, 1.0f );
				break;

			case SemiTransparency::ReverseSubtract:
				glBlendFunc( GL_ONE, GL_ONE );
				glBlendEquation( GL_FUNC_REVERSE_SUBTRACT );
				glUniform1f( m_alphaLoc, 1.0f );
				break;

			case SemiTransparency::AddQuarter:
				glBlendFunc( GL_SRC_ALPHA, GL_ONE );
				glBlendEquation( GL_FUNC_ADD );
				glUniform1f( m_alphaLoc, 0.25f );
				break;
		}
	}
	else
	{
		glDisable( GL_BLEND );
		glUniform1f( m_alphaLoc, 1.0f );
	}
}

void Renderer::PushTriangle( const Vertex vertices[ 3 ], bool semiTransparent )
{
	if ( m_vertices.size() + 3 > VertexBufferSize )
		DrawBatch();

	if ( m_drawAreaLeft >= m_drawAreaRight || m_drawAreaTop >= m_drawAreaBottom )
	{
		dbLogWarning( "Renderer::PushTriangle() -- draw area is invalid" );
		return;
	}

	SetSemiTransparencyEnabled( semiTransparent );

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

	m_renderedPrimitive = true;
}

void Renderer::PushQuad( const Vertex vertices[ 4 ], bool semiTransparent )
{
	PushTriangle( vertices, semiTransparent );
	PushTriangle( vertices + 1, semiTransparent );
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

	UpdateBlendMode();

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
	DrawBatch();

	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_BLEND );

	if ( m_viewVRam )
	{
		m_vramDrawFrameBuffer.Unbind();
		m_noAttributeVAO.Bind();
		m_fullscreenShader.Bind();
		m_vramDrawTexture.Bind();
		glViewport( 0, 0, VRamWidth, VRamHeight );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	}
	else
	{
		m_vramDrawFrameBuffer.Unbind( Render::FrameBufferBinding::Draw );
		m_vramDrawFrameBuffer.Bind( Render::FrameBufferBinding::Read );
		glViewport( 0, 0, m_displayWidth, m_displayHeight );

		glBlitFramebuffer(
			// src
			m_displayX, m_displayY, m_displayX + m_displayWidth, m_displayY + m_displayHeight,
			// dest (must display upside-down)
			0, m_displayHeight, m_displayWidth, 0,
			GL_COLOR_BUFFER_BIT, GL_NEAREST );
	}

	dbCheckRenderErrors();

	SDL_GL_SwapWindow( m_window );

	RestoreRenderState();

	m_renderedPrimitive = false;
}

}