#include "Renderer.h"

#include "ClutShader.h"
#include "FullscreenShader.h"
#include "Output16bitShader.h"
#include "Output24bitShader.h"

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

	// create no attribute VAO for fullscreen quad rendering
	m_noAttributeVAO = Render::VertexArrayObject::Create();

	// create VAO to attach attributes and shader to
	m_vramDrawVAO = Render::VertexArrayObject::Create();
	m_vramDrawVAO.Bind();

	// create vertex buffer
	m_vertexBuffer = Render::ArrayBuffer::Create();
	m_vertexBuffer.SetData<Vertex>( Render::BufferUsage::StreamDraw, VertexBufferSize );
	m_vertices.reserve( VertexBufferSize );
	
	// create fullscreen shader
	m_fullscreenShader = Render::Shader::Compile( FullscreenVertexShader, FullscreenFragmentShader );
	dbAssert( m_fullscreenShader.Valid() );

	// create clut shader
	m_clutShader = Render::Shader::Compile( ClutVertexShader, ClutFragmentShader );
	dbAssert( m_clutShader.Valid() );
	m_originLoc = m_clutShader.GetUniformLocation( "u_origin" );
	m_srcBlendLoc = m_clutShader.GetUniformLocation( "u_srcBlend" );
	m_destBlendLoc = m_clutShader.GetUniformLocation( "u_destBlend" );
	m_texWindowMask = m_clutShader.GetUniformLocation( "u_texWindowMask" );
	m_texWindowOffset = m_clutShader.GetUniformLocation( "u_texWindowOffset" );
	m_drawOpaquePixelsLoc = m_clutShader.GetUniformLocation( "u_drawOpaquePixels" );
	m_drawTransparentPixelsLoc = m_clutShader.GetUniformLocation( "u_drawTransparentPixels" );

	// create output 24bpp shader
	m_output24bppShader = Render::Shader::Compile( Output24bitVertexShader, Output24bitFragmentShader );
	dbAssert( m_output24bppShader.Valid() );
	m_srcRect24Loc = m_output24bppShader.GetUniformLocation( "u_srcRect" );

	// create output 16bpp shader
	m_output16bppShader = Render::Shader::Compile( Output16bitVertexShader, Output16bitFragmentShader );
	dbAssert( m_output16bppShader.Valid() );
	m_srcRect16Loc = m_output16bppShader.GetUniformLocation( "u_srcRect" );

	// set shader attribute locations in VAO
	constexpr auto Stride = sizeof( Vertex );

	m_clutShader.Bind();
	m_vramDrawVAO.AddFloatAttribute( m_clutShader.GetAttributeLocation( "v_pos" ), 2, Render::Type::Short, false, Stride, offsetof( Vertex, Vertex::position ) );
	m_vramDrawVAO.AddFloatAttribute( m_clutShader.GetAttributeLocation( "v_color" ), 3, Render::Type::UByte, true, Stride, offsetof( Vertex, Vertex::color ) );
	m_vramDrawVAO.AddFloatAttribute( m_clutShader.GetAttributeLocation( "v_texCoord" ), 2, Render::Type::UShort, false, Stride, offsetof( Vertex, Vertex::texCoord ) );
	m_vramDrawVAO.AddIntAttribute( m_clutShader.GetAttributeLocation( "v_clut" ), 1, Render::Type::UShort, Stride, offsetof( Vertex, Vertex::clut ) );
	m_vramDrawVAO.AddIntAttribute( m_clutShader.GetAttributeLocation( "v_texPage" ), 1, Render::Type::UShort, Stride, offsetof( Vertex, Vertex::texPage ) );
	
	// get shader uniform locations

	// VRAM draw texture
	m_vramDrawFrameBuffer = Render::FrameBuffer::Create();
	m_vramDrawTexture = Render::Texture2D::Create( Render::InternalFormat::RGBA8, VRamWidth, VRamHeight, Render::PixelFormat::RGBA, Render::PixelType::UByte );
	m_vramDrawFrameBuffer.AttachTexture( Render::AttachmentType::Color, m_vramDrawTexture );
	m_depthBuffer = Render::Texture2D::Create( Render::InternalFormat::Depth, VRamWidth, VRamHeight, Render::PixelFormat::Depth, Render::PixelType::UByte );
	m_vramDrawFrameBuffer.AttachTexture( Render::AttachmentType::Depth, m_depthBuffer );
	dbAssert( m_vramDrawFrameBuffer.IsComplete() );
	m_vramDrawFrameBuffer.Unbind();

	// VRAM read texture
	m_vramReadFrameBuffer = Render::FrameBuffer::Create();
	m_vramReadTexture = Render::Texture2D::Create( Render::InternalFormat::RGBA8, VRamWidth, VRamHeight, Render::PixelFormat::RGBA, Render::PixelType::UByte );
	m_vramReadFrameBuffer.AttachTexture( Render::AttachmentType::Color, m_vramReadTexture );
	dbAssert( m_vramReadFrameBuffer.IsComplete() );
	m_vramReadFrameBuffer.Unbind();

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
}

void Renderer::EnableVRamView( bool enable )
{
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
	m_displayWidth = w;
	m_displayHeight = h;
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

void Renderer::SetSemiTransparencyMode( SemiTransparencyMode semiTransparencyMode )
{
	if ( m_semiTransparencyMode != semiTransparencyMode )
	{
		if ( m_semiTransparencyEnabled )
			DrawBatch();

		m_semiTransparencyMode = semiTransparencyMode;

		if ( m_semiTransparencyEnabled )
			UpdateBlendMode();
	}
}

void Renderer::SetMaskBits( bool setMask, bool checkMask )
{
	if ( m_setMask != setMask || m_checkMask != checkMask )
	{
		DrawBatch();

		m_setMask = setMask;
		m_checkMask = checkMask;
		UpdateBlendMode();
		UpdateDepthTest();
	}
}

void Renderer::EnableSemiTransparency( bool enabled )
{
	if ( m_semiTransparencyEnabled != enabled )
	{
		DrawBatch();

		m_semiTransparencyEnabled = enabled;
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

void Renderer::FillVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, float r, float g, float b, float a )
{
	// TODO: check draw mode areas
	DrawBatch();

	glClearColor( r, g, b, a );
	glClearDepth( a );
	glScissor( left, top, width, height );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

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

void Renderer::SetDrawMode( TexPage texPage, ClutAttribute clut )
{
	if ( m_texPage.value == texPage.value && m_clut.value == clut.value )
		return;

	if ( m_texPage.textureDisable != texPage.textureDisable )
		DrawBatch();

	m_texPage = texPage;
	m_clut = clut;

	// 5-6   Semi Transparency     (0=B/2+F/2, 1=B+F, 2=B-F, 3=B+F/4)   ;GPUSTAT.5-6
	SetSemiTransparencyMode( static_cast<SemiTransparencyMode>( texPage.semiTransparencymode ) );

	if ( texPage.textureDisable )
		return; // textures are disabled

	const auto colorMode = texPage.texturePageColors;

	const int texBaseX = texPage.texturePageBaseX * TexturePageBaseXMult;
	const int texBaseY = texPage.texturePageBaseY * TexturePageBaseYMult;
	const int texSize = 64 << colorMode;
	const Rect texRect( texBaseX, texBaseY, texSize, texSize );

	if ( m_dirtyArea.Intersects( texRect ) )
	{
		UpdateReadTexture();
	}
	else if ( colorMode < 2 )
	{
		const int clutBaseX = clut.x * ClutBaseXMult;
		const int clutBaseY = clut.y * ClutBaseYMult;
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
	if ( m_semiTransparencyEnabled )
	{
		glEnable( GL_BLEND );

		GLenum rgbEquation = GL_FUNC_ADD;
		float srcBlend = 1.0f;
		float destBlend = 1.0f;
		switch ( m_semiTransparencyMode )
		{
			case SemiTransparencyMode::Blend:
				srcBlend = 0.5f;
				destBlend = 0.5f;
				break;

			case SemiTransparencyMode::Add:
				break;

			case SemiTransparencyMode::ReverseSubtract:
				rgbEquation = GL_FUNC_REVERSE_SUBTRACT;
				break;

			case SemiTransparencyMode::AddQuarter:
				srcBlend = 0.25;
				break;
		}

		glBlendEquationSeparate( rgbEquation, GL_FUNC_ADD );
		glBlendFuncSeparate( GL_SRC1_ALPHA, GL_SRC1_COLOR, GL_ONE, GL_ZERO );

		m_uniform.srcBlend = srcBlend;
		m_uniform.destBlend = destBlend;

		glUniform1f( m_srcBlendLoc, srcBlend );
		glUniform1f( m_destBlendLoc, destBlend );
	}
	else
	{
		glDisable( GL_BLEND );
	}
}

void Renderer::UpdateDepthTest()
{
	glDepthFunc( m_checkMask ? GL_GEQUAL : GL_ALWAYS );
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

	EnableSemiTransparency( semiTransparent );

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

	if ( m_semiTransparencyEnabled && ( m_semiTransparencyMode == SemiTransparencyMode::ReverseSubtract ) && !m_texPage.textureDisable )
	{
		// must do 2 passes for BG-FG with textures since transparency can be disabled per-pixel

		// opaque only
		glDisable( GL_BLEND );
		glUniform1i( m_drawTransparentPixelsLoc, false );
		glDrawArrays( GL_TRIANGLES, 0, m_vertices.size() );

		// transparent only
		glEnable( GL_BLEND );
		glUniform1i( m_drawOpaquePixelsLoc, false );
		glUniform1i( m_drawTransparentPixelsLoc, true );
		glDrawArrays( GL_TRIANGLES, 0, m_vertices.size() );

		glUniform1i( m_drawOpaquePixelsLoc, true );
	}
	else
	{
		glDrawArrays( GL_TRIANGLES, 0, m_vertices.size() );
		dbCheckRenderErrors();
	}

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

	glDisable( GL_CULL_FACE );
	glEnable( GL_SCISSOR_TEST );
	glEnable( GL_DEPTH_TEST );

	UpdateScissorRect();
	UpdateBlendMode();
	UpdateDepthTest();

	// restore uniforms
	// TODO: use uniform buffer?
	glUniform2f( m_originLoc, static_cast<GLfloat>( m_uniform.originX ), static_cast<GLfloat>( m_uniform.originY ) );
	glUniform1f( m_srcBlendLoc, m_uniform.srcBlend );
	glUniform1f( m_destBlendLoc, m_uniform.destBlend );
	glUniform2i( m_texWindowMask, m_uniform.texWindowMaskX, m_uniform.texWindowMaskY );
	glUniform2i( m_texWindowOffset, m_uniform.texWindowOffsetX, m_uniform.texWindowOffsetY );
	glUniform1i( m_drawOpaquePixelsLoc, true );
	glUniform1i( m_drawTransparentPixelsLoc, true );

	glViewport( 0, 0, VRamWidth, VRamHeight );

	dbCheckRenderErrors();
}

void Renderer::DisplayFrame()
{
	DrawBatch();

	// reset render state
	m_vramDrawFrameBuffer.Unbind();
	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_BLEND );
	glDisable( GL_DEPTH_TEST );

	// clear window
	int winWidth = 0;
	int winHeight = 0;
	SDL_GetWindowSize( m_window, &winWidth, &winHeight );
	glViewport( 0, 0, winWidth, winHeight );
	glClearColor( 0, 0, 0, 1 );
	glClear( GL_COLOR_BUFFER_BIT );

	if ( m_viewVRam )
	{
		m_noAttributeVAO.Bind();
		m_fullscreenShader.Bind();
		m_vramDrawTexture.Bind();
		glViewport( 0, 0, VRamWidth, VRamHeight );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	}
	else
	{
		float renderScale = std::min( (float)winWidth / (float)m_displayWidth, (float)winHeight / (float)m_displayHeight );
		if ( !m_stretchToFit )
			renderScale = std::max( 1.0f, std::floor( renderScale ) );

		const int renderWidth = static_cast<int>( m_displayWidth * renderScale );
		const int renderHeight = static_cast<int>( m_displayHeight * renderScale );
		const int renderX = ( winWidth - renderWidth ) / 2;
		const int renderY = ( winHeight - renderHeight ) / 2;

		glViewport( renderX, renderY, renderWidth, renderHeight );

		m_noAttributeVAO.Bind();
		m_vramDrawTexture.Bind();

		if ( m_colorDepth == DisplayAreaColorDepth::B24 )
		{
			m_output24bppShader.Bind();
			glUniform4i( m_srcRect24Loc, m_displayX, m_displayY, m_displayWidth, m_displayHeight );
		}
		else
		{
			m_output16bppShader.Bind();
			glUniform4i( m_srcRect16Loc, m_displayX, m_displayY, m_displayWidth, m_displayHeight );
		}

		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	}

	dbCheckRenderErrors();

	SDL_GL_SwapWindow( m_window );

	RestoreRenderState();
}

}