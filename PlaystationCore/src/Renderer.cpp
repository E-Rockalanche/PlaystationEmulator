#include "Renderer.h"

#include "ClutShader.h"
#include "DisplayShader.h"
#include "VRamViewShader.h"
#include "Output16bitShader.h"
#include "Output24bitShader.h"
#include "ResetDepthShader.h"

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
	m_vertexBuffer = Render::ArrayBuffer::Create<Vertex>( Render::BufferUsage::StreamDraw, VertexBufferSize );
	m_vertices.reserve( VertexBufferSize );
	
	// create fullscreen shader
	m_vramViewShader = Render::Shader::Compile( VRamViewVertexShader, VRamViewFragmentShader );
	dbAssert( m_vramViewShader.Valid() );

	// create clut shader
	m_clutShader = Render::Shader::Compile( ClutVertexShader, ClutFragmentShader );
	dbAssert( m_clutShader.Valid() );
	m_srcBlendLoc = m_clutShader.GetUniformLocation( "u_srcBlend" );
	m_destBlendLoc = m_clutShader.GetUniformLocation( "u_destBlend" );
	m_setMaskBitLoc = m_clutShader.GetUniformLocation( "u_setMaskBit" );
	m_drawOpaquePixelsLoc = m_clutShader.GetUniformLocation( "u_drawOpaquePixels" );
	m_drawTransparentPixelsLoc = m_clutShader.GetUniformLocation( "u_drawTransparentPixels" );
	m_ditherLoc = m_clutShader.GetUniformLocation( "u_dither" );
	m_realColorLoc = m_clutShader.GetUniformLocation( "u_realColor" );
	m_texWindowMask = m_clutShader.GetUniformLocation( "u_texWindowMask" );
	m_texWindowOffset = m_clutShader.GetUniformLocation( "u_texWindowOffset" );

	// create output 24bpp shader
	m_output24bppShader = Render::Shader::Compile( Output24bitVertexShader, Output24bitFragmentShader );
	dbAssert( m_output24bppShader.Valid() );
	m_srcRect24Loc = m_output24bppShader.GetUniformLocation( "u_srcRect" );

	// create output 16bpp shader
	m_output16bppShader = Render::Shader::Compile( Output16bitVertexShader, Output16bitFragmentShader );
	dbAssert( m_output16bppShader.Valid() );
	m_srcRect16Loc = m_output16bppShader.GetUniformLocation( "u_srcRect" );

	m_vramCopyShader.Initialize();

	m_resetDepthShader = Render::Shader::Compile( ResetDepthVertexShader, ResetDepthFragmentShader );
	dbAssert( m_resetDepthShader.Valid() );

	// create display shader
	m_displayShader = Render::Shader::Compile( DisplayVertexShader, DisplayFragmentShader );
	dbAssert( m_displayShader.Valid() );

	// set shader attribute locations in VAO
	constexpr auto Stride = sizeof( Vertex );

	m_clutShader.Bind();
	m_vramDrawVAO.AddFloatAttribute( m_clutShader.GetAttributeLocation( "v_pos" ), 4, Render::Type::Short, false, Stride, offsetof( Vertex, Vertex::position ) );
	m_vramDrawVAO.AddFloatAttribute( m_clutShader.GetAttributeLocation( "v_color" ), 3, Render::Type::UByte, true, Stride, offsetof( Vertex, Vertex::color ) );
	m_vramDrawVAO.AddFloatAttribute( m_clutShader.GetAttributeLocation( "v_texCoord" ), 2, Render::Type::UShort, false, Stride, offsetof( Vertex, Vertex::texCoord ) );
	m_vramDrawVAO.AddIntAttribute( m_clutShader.GetAttributeLocation( "v_clut" ), 1, Render::Type::UShort, Stride, offsetof( Vertex, Vertex::clut ) );
	m_vramDrawVAO.AddIntAttribute( m_clutShader.GetAttributeLocation( "v_texPage" ), 1, Render::Type::UShort, Stride, offsetof( Vertex, Vertex::texPage ) );
	
	// get shader uniform locations

	// VRAM draw texture
	m_vramDrawFramebuffer = Render::Framebuffer::Create();
	m_vramDrawTexture = Render::Texture2D::Create( Render::InternalFormat::RGBA8, VRamWidth, VRamHeight, Render::PixelFormat::RGBA, Render::PixelType::UByte );
	m_vramDrawFramebuffer.AttachTexture( Render::AttachmentType::Color, m_vramDrawTexture );
	m_vramDrawDepthBuffer = Render::Texture2D::Create( Render::InternalFormat::Depth, VRamWidth, VRamHeight, Render::PixelFormat::Depth, Render::PixelType::Short );
	m_vramDrawFramebuffer.AttachTexture( Render::AttachmentType::Depth, m_vramDrawDepthBuffer );
	dbAssert( m_vramDrawFramebuffer.IsComplete() );
	m_vramDrawFramebuffer.Unbind();

	// VRAM read texture
	m_vramReadFramebuffer = Render::Framebuffer::Create();
	m_vramReadTexture = Render::Texture2D::Create( Render::InternalFormat::RGBA8, VRamWidth, VRamHeight, Render::PixelFormat::RGBA, Render::PixelType::UByte );
	m_vramReadFramebuffer.AttachTexture( Render::AttachmentType::Color, m_vramReadTexture );
	dbAssert( m_vramReadFramebuffer.IsComplete() );
	m_vramReadFramebuffer.Unbind();

	// VRAM transfer texture
	m_vramTransferFramebuffer = Render::Framebuffer::Create();
	m_vramTransferTexture = Render::Texture2D::Create();
	m_vramTransferFramebuffer.AttachTexture( Render::AttachmentType::Color, m_vramTransferTexture );
	m_vramTransferFramebuffer.Unbind();

	// display texture
	m_displayFramebuffer = Render::Framebuffer::Create();
	m_displayTexture = Render::Texture2D::Create();
	m_displayTexture.SetLinearFilering( true );
	m_displayFramebuffer.AttachTexture( Render::AttachmentType::Color, m_displayTexture );
	m_displayFramebuffer.Unbind();

	// get ready to render!
	RestoreRenderState();

	return true;
}

void Renderer::Reset()
{
	// clear VRAM
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClearDepth( 1.0 );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	m_vertices.clear();
	ResetDirtyArea();

	m_currentDepth = std::numeric_limits<int16_t>::max() - 1;

	// GPU will reset uniforms
}

void Renderer::EnableVRamView( bool enable )
{
	if ( !m_viewVRam && enable )
	{
		SDL_SetWindowSize( m_window, VRamWidth, VRamHeight );
	}
	else if ( m_viewVRam && !enable )
	{
		SDL_SetWindowSize( m_window, 640, 480 );
	}

	m_viewVRam = enable;
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
	if ( m_drawArea.left != left || m_drawArea.top != top || m_drawArea.right != right || m_drawArea.bottom != bottom )
	{
		DrawBatch();

		m_drawArea = Math::Rectangle{ left, top, right, bottom };

		UpdateScissorRect();

		// grow dirty area once instead of for each render primitive
		m_dirtyArea.Grow( DirtyArea( left, top, right + 1, bottom + 1 ) );
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
	if ( m_forceMaskBit != setMask || m_checkMaskBit != checkMask )
	{
		DrawBatch();

		m_forceMaskBit = setMask;
		m_checkMaskBit = checkMask;
		UpdateMaskBits();
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
	dbExpects( left < VRamWidth );
	dbExpects( top < VRamHeight );
	dbExpects( width <= VRamWidth );
	dbExpects( height <= VRamHeight );

	dbLogDebug( "Renderer::UpdateVRam -- pos: %u, %u, size: %u, %u", left, top, width, height );

	DrawBatch();

	glPixelStorei( GL_UNPACK_ALIGNMENT, ( width % 2 ) ? 2 : 4 );

	const bool wrapX = left + width > VRamWidth;
	const bool wrapY = top + height > VRamHeight;

	if ( !wrapX && !wrapY && !m_checkMaskBit && !m_forceMaskBit )
	{
		m_dirtyArea.Grow( DirtyArea::FromExtents( left, top, width, height ) );
		m_vramDrawTexture.SubImage( left, top, width, height, Render::PixelFormat::RGBA, Render::PixelType::UShort_1_5_5_5_Rev, pixels );
		ResetDepthBuffer();
	}
	else
	{
		dbLogDebug( "\tvram update wrapping" );

		m_vramTransferTexture.UpdateImage( Render::InternalFormat::RGBA, width, height, Render::PixelFormat::RGBA, Render::PixelType::UShort_1_5_5_5_Rev, pixels );

		// calculate width and height segments for wrapping vram upload
		const uint32_t width2 = wrapX ? ( ( left + width ) % VRamWidth ) : 0;
		const uint32_t height2 = wrapY ? ( ( top + height ) % VRamHeight ) : 0;
		const uint32_t width1 = width - width2;
		const uint32_t height1 = height - height2;

		const float width1f = static_cast<float>( width1 ) / static_cast<float>( width );
		const float height1f = static_cast<float>( height1 ) / static_cast<float>( height );
		const float width2f = static_cast<float>( width2 ) / static_cast<float>( width );
		const float height2f = static_cast<float>( height2 ) / static_cast<float>( height );

		glDisable( GL_BLEND );
		glDisable( GL_SCISSOR_TEST );

		m_noAttributeVAO.Bind();
		m_vramCopyShader.Use( 0, 0, width1f, height1f, m_forceMaskBit );
		m_vramTransferTexture.Bind();

		// bottom right
		m_dirtyArea.Grow( DirtyArea::FromExtents( left, top, width1, height1 ) );
		glViewport( left, top, width1, height1 );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

		// bottom left
		if ( wrapX )
		{
			m_vramCopyShader.SetSourceArea( width1f, 0, width2f, height1f );
			m_dirtyArea.Grow( 0, top );
			glViewport( 0, top, width2, height1 );
			glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		}

		// top right
		if ( wrapY )
		{
			m_vramCopyShader.SetSourceArea( 0, height1f, width1f, height2f );
			m_dirtyArea.Grow( left, 0 );
			glViewport( left, 0, width1, height2 );
			glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		}

		// top left
		if ( wrapX && wrapY )
		{
			m_vramCopyShader.SetSourceArea( width1f, height1f, width2f, height2f );
			glViewport( 0, 0, width2, height2 );
			glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		}

		RestoreRenderState();
	}

	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );

	dbCheckRenderErrors();
}

void Renderer::ReadVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint16_t* vram )
{
	dbLogDebug( "Renderer::ReadVRam -- pos: %u, %u, size: %u, %u", left, top, width, height );

	DrawBatch();

	// handle wrapping
	if ( left + width > VRamWidth )
	{
		left = 0;
		width = VRamWidth;
	}

	if ( top + height > VRamHeight )
	{
		top = 0;
		height = VRamHeight;
	}

	// copy vram area to new texture
	m_vramTransferTexture.UpdateImage( Render::InternalFormat::RGBA, width, height, Render::PixelFormat::RGBA, Render::PixelType::UShort_1_5_5_5_Rev );
	dbAssert( m_vramTransferFramebuffer.IsComplete() );
	m_vramTransferFramebuffer.Bind( Render::FramebufferBinding::Draw );
	m_vramDrawFramebuffer.Bind( Render::FramebufferBinding::Read );
	glDisable( GL_SCISSOR_TEST );
	glBlitFramebuffer( left, top, left + width, top + height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST );
	dbCheckRenderErrors();

	// unpack pixel data into vram read-back array
	m_vramTransferFramebuffer.Bind( Render::FramebufferBinding::Read );
	glPixelStorei( GL_PACK_ALIGNMENT, 2 );
	glPixelStorei( GL_PACK_ROW_LENGTH, VRamWidth );
	glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, vram + left + top * VRamWidth );
	dbCheckRenderErrors();

	// reset render state
	m_vramDrawFramebuffer.Bind();
	glEnable( GL_SCISSOR_TEST );
	glPixelStorei( GL_PACK_ALIGNMENT, 4 );
	glPixelStorei( GL_PACK_ROW_LENGTH, 0 );
	dbCheckRenderErrors();
}

void Renderer::FillVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, float r, float g, float b, float a )
{
	if ( width == 0 || height == 0 )
		return;

	DrawBatch();

	glClearColor( r, g, b, a );
	glClearDepth( a );

	const bool wrapX = left + width > VRamWidth;
	const bool wrapY = top + height > VRamHeight;

	const uint32_t width2 = wrapX ? ( left + width - VRamWidth ) : 0;
	const uint32_t height2 = wrapX ? ( top + height - VRamHeight ) : 0;
	const uint32_t width1 = width - width2;
	const uint32_t height1 = height - height2;

	m_dirtyArea.Grow( DirtyArea::FromExtents( left, top, width1, height1 ) );
	glScissor( left, top, width1, height1 );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	if ( wrapX )
	{
		m_dirtyArea.Grow( 0, top );
		glScissor( 0, top, width2, height1 );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}

	if ( wrapY )
	{
		m_dirtyArea.Grow( left, 0 );
		glScissor( left, 0, width1, height2 );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}

	if ( wrapX && wrapY )
	{
		glScissor( 0, 0, width2, height2 );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}

	dbCheckRenderErrors();

	UpdateScissorRect();
}

void Renderer::CopyVRam( int srcX, int srcY, int destX, int destY, int width, int height )
{
	dbExpects( srcX + width <= VRamWidth );
	dbExpects( srcY + height <= VRamHeight );
	dbExpects( destX + width <= VRamWidth );
	dbExpects( destY + height <= VRamHeight );

	DrawBatch();

	if ( m_dirtyArea.Intersects( DirtyArea::FromExtents( srcX, srcY, width, height ) ) )
		UpdateReadTexture();

	if ( m_checkMaskBit || m_forceMaskBit )
	{
		glDisable( GL_BLEND );
		glDisable( GL_SCISSOR_TEST );
		glViewport( destX, destY, width, height );

		m_noAttributeVAO.Bind();
		m_vramCopyShader.Use(
			srcX / VRamWidthF, srcY / VRamHeightF, width / VRamWidthF, height / VRamHeightF,
			m_forceMaskBit );

		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

		dbCheckRenderErrors();

		RestoreRenderState();
	}
	else
	{
		m_vramReadFramebuffer.Bind( Render::FramebufferBinding::Read );
		glDisable( GL_SCISSOR_TEST );

		glBlitFramebuffer(
			srcX, srcY, srcX + width, srcY + height,
			destX, destY, destX + width, destY + height,
			GL_COLOR_BUFFER_BIT, GL_NEAREST );

		m_vramDrawFramebuffer.Bind( Render::FramebufferBinding::Read );
		glEnable( GL_SCISSOR_TEST );

		dbCheckRenderErrors();
	}

	m_dirtyArea.Grow( DirtyArea::FromExtents( destX, destY, width, height ) );
}

void Renderer::SetDrawMode( TexPage texPage, ClutAttribute clut, bool dither )
{
	if ( m_realColor )
		dither = false;

	const bool ditherDiff = m_dither != dither;
	const bool drawModeDiff = m_texPage.value != texPage.value || m_clut.value != clut.value;

	if ( drawModeDiff || ditherDiff )
		DrawBatch();

	if ( ditherDiff )
	{
		m_dither = dither;
		glUniform1i( m_ditherLoc, dither );
	}

	if ( drawModeDiff )
	{
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
		const DirtyArea texRect( texBaseX, texBaseY, texSize, texSize );

		if ( m_dirtyArea.Intersects( texRect ) )
		{
			UpdateReadTexture();
		}
		else if ( colorMode < 2 )
		{
			const int clutBaseX = clut.x * ClutBaseXMult;
			const int clutBaseY = clut.y * ClutBaseYMult;
			const DirtyArea clutRect( clutBaseX, clutBaseY, 32 << colorMode, ClutHeight );

			if ( m_dirtyArea.Intersects( clutRect ) )
				UpdateReadTexture();
		}
	}
}

void Renderer::SetDisplayArea( const DisplayArea& vramDisplayArea, const DisplayArea& targetDisplayArea, float aspectRatio )
{
	if ( m_displayTexture.GetWidth() != static_cast<GLsizei>( targetDisplayArea.width ) ||
		m_displayTexture.GetHeight() != static_cast<GLsizei>( targetDisplayArea.height ) )
	{
		// update display texture size (usually when switching between NTSC and PAL)
		m_displayTexture.UpdateImage( Render::InternalFormat::RGB, targetDisplayArea.width, targetDisplayArea.height, Render::PixelFormat::RGB, Render::PixelType::UByte );
	}

	m_vramDisplayArea = vramDisplayArea;
	m_targetDisplayArea = targetDisplayArea;
	m_aspectRatio = aspectRatio;
}

void Renderer::SetRealColor( bool realColor )
{
	if ( m_realColor != realColor )
	{
		m_realColor = realColor;
		glUniform1i( m_realColorLoc, realColor );
	}
}

void Renderer::UpdateScissorRect()
{
	const auto width = std::max<int>( m_drawArea.right - m_drawArea.left + 1, 0 );
	const auto height = std::max<int>( m_drawArea.bottom - m_drawArea.top + 1, 0 );
	glScissor( m_drawArea.left, m_drawArea.top, width, height );
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
				srcBlend = 0.25f;
				break;
		}

		glBlendEquationSeparate( rgbEquation, GL_FUNC_ADD );
		glBlendFuncSeparate( GL_ONE, GL_SRC1_ALPHA, GL_ONE, GL_ZERO );

		m_uniform.srcBlend = srcBlend;
		m_uniform.destBlend = destBlend;

		glUniform1f( m_srcBlendLoc, srcBlend );
		glUniform1f( m_destBlendLoc, destBlend );
	}
	else
	{
		glDisable( GL_BLEND );
	}

	dbCheckRenderErrors();
}

void Renderer::UpdateMaskBits()
{
	glUniform1i( m_setMaskBitLoc, m_forceMaskBit );
	glDepthFunc( m_checkMaskBit ? GL_LESS : GL_ALWAYS );
}

void Renderer::PushTriangle( Vertex vertices[ 3 ], bool semiTransparent )
{
	if ( m_vertices.size() + 3 > VertexBufferSize )
		DrawBatch();

	if ( m_drawArea.left >= m_drawArea.right || m_drawArea.top >= m_drawArea.bottom )
		return;

	if ( m_checkMaskBit )
	{
		if ( m_currentDepth == 0 )
			ResetDepthBuffer();

		std::for_each_n( vertices, 3, [this]( auto& v ) { v.position.z = m_currentDepth; } );
		--m_currentDepth;
	}

	EnableSemiTransparency( semiTransparent );

	m_vertices.insert( m_vertices.end(), vertices, vertices + 3 );
}

void Renderer::PushQuad( Vertex vertices[ 4 ], bool semiTransparent )
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
	}

	dbCheckRenderErrors();

	m_vertices.clear();
}

void Renderer::ResetDepthBuffer()
{
	m_currentDepth = std::numeric_limits<int16_t>::max() - 1;

	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_BLEND );
	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	glDepthFunc( GL_ALWAYS );
	glViewport( 0, 0, VRamWidth, VRamHeight );

	m_vramDrawTexture.Bind();
	m_resetDepthShader.Bind();
	m_noAttributeVAO.Bind();
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	dbCheckRenderErrors();

	RestoreRenderState();
}

void Renderer::UpdateReadTexture()
{
	if ( m_dirtyArea.GetWidth() == 0 || m_dirtyArea.GetHeight() == 0 )
		return;

	m_vramReadFramebuffer.Bind( Render::FramebufferBinding::Draw );
	glDisable( GL_SCISSOR_TEST );

	glBlitFramebuffer( 
		m_dirtyArea.left, m_dirtyArea.top, m_dirtyArea.right, m_dirtyArea.bottom,
		m_dirtyArea.left, m_dirtyArea.top, m_dirtyArea.right, m_dirtyArea.bottom,
		GL_COLOR_BUFFER_BIT, GL_NEAREST );

	m_vramDrawFramebuffer.Bind( Render::FramebufferBinding::Draw );
	glEnable( GL_SCISSOR_TEST );

	dbCheckRenderErrors();

	ResetDirtyArea();
}

void Renderer::RestoreRenderState()
{
	m_vramDrawVAO.Bind();
	m_vramDrawFramebuffer.Bind();
	m_vramReadTexture.Bind();
	m_clutShader.Bind();

	glDisable( GL_CULL_FACE );
	glEnable( GL_SCISSOR_TEST );
	glEnable( GL_DEPTH_TEST );

	UpdateScissorRect();
	UpdateBlendMode();
	UpdateMaskBits();

	// restore uniforms
	// TODO: use uniform buffer?
	glUniform1f( m_srcBlendLoc, m_uniform.srcBlend );
	glUniform1f( m_destBlendLoc, m_uniform.destBlend );
	glUniform1i( m_drawOpaquePixelsLoc, true );
	glUniform1i( m_drawTransparentPixelsLoc, true );
	glUniform1i( m_ditherLoc, m_dither );
	glUniform1i( m_realColorLoc, m_realColor );
	glUniform2i( m_texWindowMask, m_uniform.texWindowMaskX, m_uniform.texWindowMaskY );
	glUniform2i( m_texWindowOffset, m_uniform.texWindowOffsetX, m_uniform.texWindowOffsetY );

	glViewport( 0, 0, VRamWidth, VRamHeight );

	dbCheckRenderErrors();
}

void Renderer::DisplayFrame()
{
	DrawBatch();

	// reset render state
	m_vramDrawFramebuffer.Unbind();
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
		m_vramViewShader.Bind();
		m_vramDrawTexture.Bind();
		glViewport( 0, 0, VRamWidth, VRamHeight );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	}
	else if ( m_displayEnable )
	{
		// render to display texture
		m_noAttributeVAO.Bind();

		if ( m_colorDepth == DisplayAreaColorDepth::B24 )
		{
			m_output24bppShader.Bind();
			glUniform4i( m_srcRect24Loc, m_vramDisplayArea.x, m_vramDisplayArea.y, m_vramDisplayArea.width, m_vramDisplayArea.height );
		}
		else
		{
			m_output16bppShader.Bind();
			glUniform4i( m_srcRect16Loc, m_vramDisplayArea.x, m_vramDisplayArea.y, m_vramDisplayArea.width, m_vramDisplayArea.height );
		}

		m_vramDrawTexture.Bind();
		m_displayFramebuffer.Bind();
		glViewport( m_targetDisplayArea.x, m_targetDisplayArea.y, m_vramDisplayArea.width, m_vramDisplayArea.height );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		m_displayFramebuffer.Unbind();

		// render to window
		m_displayShader.Bind();
		m_displayTexture.Bind();

		const float displayWidth = static_cast<float>( m_vramDisplayArea.width );
		const float displayHeight = static_cast<float>( m_vramDisplayArea.width ) / m_aspectRatio;

		float renderScale = std::min( winWidth / displayWidth, winHeight / displayHeight );
		if ( !m_stretchToFit )
			renderScale = std::max( 1.0f, std::floor( renderScale ) );

		const int renderWidth = static_cast<int>( displayWidth * renderScale );
		const int renderHeight = static_cast<int>( displayHeight * renderScale );
		const int renderX = ( winWidth - renderWidth ) / 2;
		const int renderY = ( winHeight - renderHeight ) / 2;

		glViewport( renderX, renderY, renderWidth, renderHeight );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	}

	dbCheckRenderErrors();

	SDL_GL_SwapWindow( m_window );

	RestoreRenderState();
}

}