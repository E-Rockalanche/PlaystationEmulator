#include "Renderer.h"

#include "ClutShader.h"
#include "DisplayShader.h"
#include "VRamViewShader.h"
#include "Output16bitShader.h"
#include "Output24bitShader.h"
#include "ResetDepthShader.h"
#include "SaveState.h"

#include <Render/Types.h>

#include <stdx/assert.h>
#include <stdx/bit.h>

#include <algorithm>
#include <array>

namespace PSX
{

namespace
{

constexpr size_t VertexBufferSize = 1024;

constexpr uint32_t MaxResolutionScale = 4;

constexpr GLint GetPixelStoreAlignment( uint32_t x, uint32_t w ) noexcept
{
	const bool odd = ( x % 2 != 0 ) || ( w % 2 != 0 );
	return odd ? 2 : 4;
}

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
	m_texWindowMaskLoc = m_clutShader.GetUniformLocation( "u_texWindowMask" );
	m_texWindowOffsetLoc = m_clutShader.GetUniformLocation( "u_texWindowOffset" );
	m_resolutionScaleLoc = m_clutShader.GetUniformLocation( "u_resolutionScale" );

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

	// get shader uniform locations
	m_clutShader.Bind();
	m_vramDrawVAO.AddFloatAttribute( m_clutShader.GetAttributeLocation( "v_pos" ), 4, Render::Type::Short, false, Stride, offsetof( Vertex, Vertex::position ) );
	m_vramDrawVAO.AddFloatAttribute( m_clutShader.GetAttributeLocation( "v_color" ), 3, Render::Type::UByte, true, Stride, offsetof( Vertex, Vertex::color ) );
	m_vramDrawVAO.AddFloatAttribute( m_clutShader.GetAttributeLocation( "v_texCoord" ), 2, Render::Type::Short, false, Stride, offsetof( Vertex, Vertex::texCoord ) );
	m_vramDrawVAO.AddIntAttribute( m_clutShader.GetAttributeLocation( "v_clut" ), 1, Render::Type::UShort, Stride, offsetof( Vertex, Vertex::clut ) );
	m_vramDrawVAO.AddIntAttribute( m_clutShader.GetAttributeLocation( "v_texPage" ), 1, Render::Type::UShort, Stride, offsetof( Vertex, Vertex::texPage ) );
	
	InitializeVRamFramebuffers();

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

void Renderer::InitializeVRamFramebuffers()
{
	// VRAM draw texture
	m_vramDrawFramebuffer = Render::Framebuffer::Create();
	m_vramDrawTexture = Render::Texture2D::Create( Render::InternalFormat::RGBA8, GetVRamTextureWidth(), GetVRamTextureHeight(), Render::PixelFormat::RGBA, Render::PixelType::UByte );
	m_vramDrawFramebuffer.AttachTexture( Render::AttachmentType::Color, m_vramDrawTexture );
	m_vramDrawDepthBuffer = Render::Texture2D::Create( Render::InternalFormat::Depth16, GetVRamTextureWidth(), GetVRamTextureHeight(), Render::PixelFormat::Depth, Render::PixelType::Short );
	m_vramDrawFramebuffer.AttachTexture( Render::AttachmentType::Depth, m_vramDrawDepthBuffer );
	dbAssert( m_vramDrawFramebuffer.IsComplete() );
	m_vramDrawFramebuffer.Unbind();

	// VRAM read texture
	m_vramReadFramebuffer = Render::Framebuffer::Create();
	m_vramReadTexture = Render::Texture2D::Create( Render::InternalFormat::RGBA8, GetVRamTextureWidth(), GetVRamTextureHeight(), Render::PixelFormat::RGBA, Render::PixelType::UByte );
	m_vramReadTexture.SetTextureWrap( true );
	m_vramReadFramebuffer.AttachTexture( Render::AttachmentType::Color, m_vramReadTexture );
	dbAssert( m_vramReadFramebuffer.IsComplete() );
	m_vramReadFramebuffer.Unbind();
}

constexpr Renderer::Rect Renderer::GetWrappedBounds( uint32_t left, uint32_t top, uint32_t width, uint32_t height ) noexcept
{
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

	return Rect::FromExtents( left, top, width, height );
}

void Renderer::Reset()
{
	// clear VRAM textures

	glDisable( GL_SCISSOR_TEST );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClearDepth( 1.0 );

	m_vramReadFramebuffer.Bind();
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	m_vramDrawFramebuffer.Bind();
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	// reset GPU state

	m_vramDisplayArea = {};
	m_targetDisplayArea = {};
	m_aspectRatio = 0.0f;

	m_drawArea = {};
	m_colorDepth = DisplayAreaColorDepth::B15;

	m_semiTransparencyMode = SemiTransparencyMode::Blend;
	m_semiTransparencyEnabled = false;

	m_forceMaskBit = false;
	m_checkMaskBit = false;
	m_dither = false;
	m_displayEnable = false;

	m_texPage.value = 0;
	m_texPage.textureDisable = true;
	m_clut.value = 0;

	// reset renderer state

	m_texturePageX = 0;
	m_texturePageY = 0;

	m_texWindowMaskX = 0;
	m_texWindowMaskY = 0;
	m_texWindowOffsetX = 0;
	m_texWindowOffsetY = 0;

	m_vertices.clear();

	ResetDirtyArea();
	m_textureArea = {};
	m_clutArea = {};

	m_currentDepth = ResetDepth;

	RestoreRenderState();
}

bool Renderer::SetResolutionScale( uint32_t scale )
{
	if ( scale < 1 || scale > MaxResolutionScale )
		return false;

	if ( scale == m_resolutionScale )
		return true;

	const GLint newWidth = VRamWidth * scale;
	const GLint newHeight = VRamHeight * scale;
	const GLint maxTextureSize = Render::GetMaxTextureSize();
	if ( newWidth > maxTextureSize || newHeight > maxTextureSize )
		return false;

	const auto oldWidth = VRamWidth * m_resolutionScale;
	const auto oldHeight = VRamHeight * m_resolutionScale;

	m_resolutionScale = scale;

	// keep old vram objects
	auto oldFramebuffer = std::move( m_vramDrawFramebuffer );
	auto oldDrawTexture = std::move( m_vramDrawTexture );
	auto oldDepthBuffer = std::move( m_vramDrawDepthBuffer );

	InitializeVRamFramebuffers();

	// copy old vram to new framebuffers
	glDisable( GL_SCISSOR_TEST );
	oldFramebuffer.Bind( Render::FramebufferBinding::Read );

	m_vramDrawFramebuffer.Bind( Render::FramebufferBinding::Draw );
	glBlitFramebuffer( 0, 0, oldWidth, oldHeight, 0, 0, newWidth, newHeight, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST );

	m_vramReadFramebuffer.Bind( Render::FramebufferBinding::Draw );
	glBlitFramebuffer( 0, 0, oldWidth, oldHeight, 0, 0, newWidth, newHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST );

	RestoreRenderState();

	return true;
}

void Renderer::SetViewport( uint32_t left, uint32_t top, uint32_t width, uint32_t height )
{
	glViewport(
		static_cast<GLint>( left * m_resolutionScale ),
		static_cast<GLint>( top * m_resolutionScale ),
		static_cast<GLsizei>( width * m_resolutionScale ),
		static_cast<GLsizei>( height * m_resolutionScale ) );
}

void Renderer::SetScissor( uint32_t left, uint32_t top, uint32_t width, uint32_t height )
{
	glScissor(
		static_cast<GLint>( left * m_resolutionScale ),
		static_cast<GLint>( top * m_resolutionScale ),
		static_cast<GLsizei>( width * m_resolutionScale ),
		static_cast<GLsizei>( height * m_resolutionScale ) );
}

void Renderer::EnableVRamView( bool enable )
{
	if ( !m_viewVRam && enable )
	{
		SDL_GetWindowSize( m_window, &m_cachedWindowWidth, &m_cachedWindowHeight );
		SDL_SetWindowSize( m_window, GetVRamTextureWidth(), GetVRamTextureHeight() );
		SDL_SetWindowResizable( m_window, SDL_FALSE );
	}
	else if ( m_viewVRam && !enable )
	{
		SDL_SetWindowSize( m_window, m_cachedWindowWidth, m_cachedWindowHeight );
		SDL_SetWindowResizable( m_window, SDL_TRUE );
	}

	m_viewVRam = enable;
}

void Renderer::SetTextureWindow( uint32_t maskX, uint32_t maskY, uint32_t offsetX, uint32_t offsetY )
{
	if ( m_texWindowMaskX != maskX || m_texWindowMaskY != maskY || m_texWindowOffsetX != offsetX || m_texWindowOffsetY != offsetY )
	{
		DrawBatch();

		m_texWindowMaskX = maskX;
		m_texWindowMaskY = maskY;
		m_texWindowOffsetX = offsetX;
		m_texWindowOffsetY = offsetY;

		glUniform2i( m_texWindowMaskLoc, maskX, maskY );
		glUniform2i( m_texWindowOffsetLoc, offsetX, offsetY );
		dbCheckRenderErrors();
	}
}

void Renderer::SetDrawArea( GLint left, GLint top, GLint right, GLint bottom )
{
	const Rect newDrawArea( left, top, right, bottom );
	if ( m_drawArea != newDrawArea )
	{
		DrawBatch();

		m_drawArea = newDrawArea;
		UpdateScissorRect();
	}
}

void Renderer::SetSemiTransparencyMode( SemiTransparencyMode semiTransparencyMode )
{
	if ( m_semiTransparencyMode != semiTransparencyMode )
	{
		if ( m_semiTransparencyEnabled )
			DrawBatch();

		dbLogDebug( "Renderer::SetSemiTransparencyMode -- [%i]", (int)semiTransparencyMode );
		dbLogDebug( "\tenabled: %s", m_semiTransparencyEnabled ? "true" : "false" );

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

		dbLogDebug( "Renderer::EnableSemiTransparency -- [%s]", enabled ? "true" : "false" );
		if ( enabled )
			dbLogDebug( "\tsemiTransparencyMode: %u", (int)m_semiTransparencyMode );

		m_semiTransparencyEnabled = enabled;
		UpdateBlendMode();
	}
}

void Renderer::GrowDirtyArea( const Rect& bounds ) noexcept
{
	// check if bounds should cover pending batched polygons
	if ( m_dirtyArea.Intersects( bounds ) )
		DrawBatch();

	m_dirtyArea.Grow( bounds );

	// check if bounds will overwrite current texture data
	if ( IntersectsTextureData( bounds ) )
		DrawBatch();
}

void Renderer::UpdateVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, const uint16_t* pixels )
{
	dbExpects( left < VRamWidth );
	dbExpects( top < VRamHeight );
	dbExpects( width > 0 );
	dbExpects( height > 0 );

	dbLogDebug( "Renderer::UpdateVRam -- pos: %u, %u, size: %u, %u", left, top, width, height );

	const auto updateBounds = GetWrappedBounds( left, top, width, height );
	GrowDirtyArea( updateBounds );

	glPixelStorei( GL_UNPACK_ALIGNMENT, GetPixelStoreAlignment( left, width ) );

	const bool wrapX = ( left + width ) > VRamWidth;
	const bool wrapY = ( top + height ) > VRamHeight;

	if ( !wrapX && !wrapY && !m_checkMaskBit && !m_forceMaskBit && m_resolutionScale == 1 )
	{
		m_vramDrawTexture.SubImage( left, top, width, height, Render::PixelFormat::RGBA, Render::PixelType::UShort_1_5_5_5_Rev, pixels );
		ResetDepthBuffer();
	}
	else
	{
		dbLogDebug( "\tvram update wrapping" );

		UpdateCurrentDepth();

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
		m_vramCopyShader.Use( 0, 0, width1f, height1f, GetNormalizedDepth(), m_forceMaskBit );
		m_vramTransferTexture.Bind();

		// bottom right
		SetViewport( left, top, width1, height1 );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

		// bottom left
		if ( wrapX )
		{
			m_vramCopyShader.SetSourceArea( width1f, 0, width2f, height1f );
			SetViewport( 0, top, width2, height1 );
			glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		}

		// top right
		if ( wrapY )
		{
			m_vramCopyShader.SetSourceArea( 0, height1f, width1f, height2f );
			SetViewport( left, 0, width1, height2 );
			glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		}

		// top left
		if ( wrapX && wrapY )
		{
			m_vramCopyShader.SetSourceArea( width1f, height1f, width2f, height2f );
			SetViewport( 0, 0, width2, height2 );
			glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		}

		RestoreRenderState();
	}

	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );

	dbCheckRenderErrors();
}

void Renderer::ReadVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint16_t* vram )
{
	dbExpects( left < VRamWidth );
	dbExpects( top < VRamHeight );
	dbExpects( width > 0 );
	dbExpects( height > 0 );

	dbLogDebug( "Renderer::ReadVRam -- pos: %u, %u, size: %u, %u", left, top, width, height );

	const auto readBounds = GetWrappedBounds( left, top, width, height );
	if ( m_dirtyArea.Intersects( readBounds ) )
		DrawBatch();

	const GLint readWidth = readBounds.GetWidth();
	const GLint readHeight = readBounds.GetHeight();

	// copy vram area to temp texture
	if ( m_vramTransferTexture.GetWidth() != readWidth || m_vramTransferTexture.GetHeight() != readHeight )
		m_vramTransferTexture.UpdateImage( Render::InternalFormat::RGBA, readWidth, readHeight, Render::PixelFormat::RGBA, Render::PixelType::UShort_1_5_5_5_Rev );

	dbAssert( m_vramTransferFramebuffer.IsComplete() );
	m_vramTransferFramebuffer.Bind( Render::FramebufferBinding::Draw );
	m_vramDrawFramebuffer.Bind( Render::FramebufferBinding::Read );
	glDisable( GL_SCISSOR_TEST );
	const Rect srcArea = readBounds * m_resolutionScale;
	glBlitFramebuffer(
		srcArea.left, srcArea.top,
		srcArea.right, srcArea.bottom,
		0, 0,
		readWidth, readHeight,
		GL_COLOR_BUFFER_BIT, GL_LINEAR ); // use linear for higher resolutions (src and dest area will differ)

	// unpack pixel data into vram read-back array
	m_vramTransferFramebuffer.Bind( Render::FramebufferBinding::Read );
	glPixelStorei( GL_PACK_ALIGNMENT, GetPixelStoreAlignment( left, width ) );
	glPixelStorei( GL_PACK_ROW_LENGTH, VRamWidth );
	glReadPixels( 0, 0, readWidth, readHeight, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, vram + readBounds.left + readBounds.top * VRamWidth );

	// reset render state
	m_vramDrawFramebuffer.Bind();
	glEnable( GL_SCISSOR_TEST );
	glPixelStorei( GL_PACK_ALIGNMENT, 4 );
	glPixelStorei( GL_PACK_ROW_LENGTH, 0 );

	dbCheckRenderErrors();
}

void Renderer::FillVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b )
{
	dbExpects( left < VRamWidth );
	dbExpects( top < VRamHeight );
	dbExpects( width > 0 );
	dbExpects( height > 0 );

	// draw batch if we are going to fill over pending polygons
	GrowDirtyArea( GetWrappedBounds( left, top, width, height ) );

	// Fills the area in the frame buffer with the value in RGB. Horizontally the filling is done in 16-pixel (32-bytes) units (see below masking/rounding).
	// The "Color" parameter is a 24bit RGB value, however, the actual fill data is 16bit: The hardware automatically converts the 24bit RGB value to 15bit RGB (with bit15=0).
	// Fill is NOT affected by the Mask settings (acts as if Mask.Bit0,1 are both zero).

	float rF, gF, bF;
	if ( m_realColor )
	{
		rF = r / 255.0f;
		gF = g / 255.0f;
		bF = b / 255.0f;
	}
	else
	{
		rF = ( r >> 3 ) / 31.0f;
		gF = ( g >> 3 ) / 31.0f;
		bF = ( b >> 3 ) / 31.0f;
	}

	static constexpr float MaskBitAlpha = 0.0f;
	static constexpr double MaskBitDepth = 1.0;

	glClearColor( rF, gF, bF, MaskBitAlpha );
	glClearDepth( MaskBitDepth );

	const bool wrapX = left + width > VRamWidth;
	const bool wrapY = top + height > VRamHeight;

	const uint32_t width2 = wrapX ? ( left + width - VRamWidth ) : 0;
	const uint32_t height2 = wrapX ? ( top + height - VRamHeight ) : 0;
	const uint32_t width1 = width - width2;
	const uint32_t height1 = height - height2;

	SetScissor( left, top, width1, height1 );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	if ( wrapX )
	{
		SetScissor( 0, top, width2, height1 );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}

	if ( wrapY )
	{
		SetScissor( left, 0, width1, height2 );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}

	if ( wrapX && wrapY )
	{
		SetScissor( 0, 0, width2, height2 );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}

	dbCheckRenderErrors();

	UpdateScissorRect();
}

void Renderer::CopyVRam( uint32_t srcX, uint32_t srcY, uint32_t destX, uint32_t destY, uint32_t width, uint32_t height )
{
	// TODO: handle wrapped copy
	dbExpects( srcX + width <= VRamWidth );
	dbExpects( srcY + height <= VRamHeight );
	dbExpects( destX + width <= VRamWidth );
	dbExpects( destY + height <= VRamHeight );

	const auto srcBounds = Rect::FromExtents( srcX, srcY, width, height );
	const auto destBounds = Rect::FromExtents( destX, destY, width, height );

	if ( m_dirtyArea.Intersects( srcBounds ) )
	{
		// update read texture if src area is dirty
		UpdateReadTexture();
		m_dirtyArea.Grow( destBounds );
	}
	else
	{
		GrowDirtyArea( destBounds );
	}

	// copy src area to dest area
	UpdateCurrentDepth();
	m_noAttributeVAO.Bind();
	m_vramCopyShader.Use( srcX / VRamWidthF, srcY / VRamHeightF, width / VRamWidthF, height / VRamHeightF, GetNormalizedDepth(), m_forceMaskBit );
	glDisable( GL_BLEND );
	glDisable( GL_SCISSOR_TEST );
	SetViewport( destX, destY, width, height );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

	dbCheckRenderErrors();

	RestoreRenderState();
}

void Renderer::SetDrawMode( TexPage texPage, ClutAttribute clut, bool dither )
{
	if ( m_realColor )
		dither = false;

	if ( m_dither != dither )
	{
		DrawBatch();

		m_dither = dither;
		glUniform1i( m_ditherLoc, dither );
	}

	static constexpr std::array<int32_t, 4> ColorModeClutWidths{ 16, 256, 0, 0 };
	static constexpr std::array<int32_t, 4> ColorModeTexturePageWidths{ TexturePageWidth / 4, TexturePageWidth / 2, TexturePageWidth, TexturePageWidth };

	auto updateClut = [&]
	{
		m_clut = clut;

		// must always calculate clut bounds even if texture mapping is currently disabled
		const int32_t clutBaseX = clut.x * ClutBaseXMult;
		const int32_t clutBaseY = clut.y * ClutBaseYMult;
		const int32_t clutWidth = ColorModeClutWidths[ texPage.texturePageColors ];
		const int32_t clutHeight = 1;
		m_clutArea = Rect::FromExtents( clutBaseX, clutBaseY, clutWidth, clutHeight );
	};

	if ( m_texPage.value != texPage.value )
	{
		DrawBatch();

		m_texPage = texPage;

		// 5-6   Semi Transparency     (0=B/2+F/2, 1=B+F, 2=B-F, 3=B+F/4)   ;GPUSTAT.5-6
		SetSemiTransparencyMode( static_cast<SemiTransparencyMode>( texPage.semiTransparencymode ) );

		if ( UsingTexture() )
		{
			const int32_t texBaseX = texPage.texturePageBaseX * TexturePageBaseXMult;
			const int32_t texBaseY = texPage.texturePageBaseY * TexturePageBaseYMult;
			const int32_t texSize = ColorModeTexturePageWidths[ texPage.texturePageColors ];
			m_textureArea = Rect::FromExtents( texBaseX, texBaseY, texSize, texSize );

			if ( UsingClut() )
				updateClut();
		}
	}
	else if ( m_clut.value != clut.value && UsingTexture() && UsingClut() )
	{
		DrawBatch();

		updateClut();
	}

	// update read texture if texpage or clut area is dirty
	if ( IntersectsTextureData( m_dirtyArea ) )
		UpdateReadTexture();
}

void Renderer::SetDisplayArea( const DisplayArea& vramDisplayArea, const DisplayArea& targetDisplayArea, float aspectRatio )
{
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
	SetScissor( m_drawArea.left, m_drawArea.top, width, height );
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
		glBlendFuncSeparate( GL_SRC1_ALPHA, GL_SRC1_COLOR, GL_ONE, GL_ZERO );

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
	glDepthFunc( m_checkMaskBit ? GL_LEQUAL : GL_ALWAYS );
}

void Renderer::PushTriangle( Vertex vertices[ 3 ], bool semiTransparent )
{
	if ( !IsDrawAreaValid() )
		return;

	// check if vertices will fit buffer
	if ( m_vertices.size() + 3 > VertexBufferSize )
		DrawBatch();

	EnableSemiTransparency( semiTransparent );

	// set triangle depth
	UpdateCurrentDepth();
	std::for_each_n( vertices, 3, [this]( auto& v )
		{ 
			m_dirtyArea.Grow( v.position.x, v.position.y );
			v.position.z = m_currentDepth;
		} );

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
	DrawBatch();

	m_currentDepth = ResetDepth;

	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_BLEND );
	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	glDepthFunc( GL_ALWAYS );

	m_vramDrawTexture.Bind();
	m_resetDepthShader.Bind();
	m_noAttributeVAO.Bind();
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	dbCheckRenderErrors();

	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	RestoreRenderState();
}

void Renderer::UpdateCurrentDepth()
{
	if ( m_checkMaskBit )
	{
		++m_currentDepth;

		if ( m_currentDepth == MaxDepth )
			ResetDepthBuffer();
	}
}

void Renderer::UpdateReadTexture()
{
	if ( m_dirtyArea.Empty() )
		return;

	DrawBatch();

	m_vramReadFramebuffer.Bind( Render::FramebufferBinding::Draw );
	glDisable( GL_SCISSOR_TEST );

	const auto blitArea = m_dirtyArea * m_resolutionScale;
	glBlitFramebuffer( 
		blitArea.left, blitArea.top, blitArea.right, blitArea.bottom,
		blitArea.left, blitArea.top, blitArea.right, blitArea.bottom,
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
	// src and dest blend set by UpdateBlendMode()
	// setMask set in UpdateMaskBits()
	glUniform1i( m_drawOpaquePixelsLoc, true );
	glUniform1i( m_drawTransparentPixelsLoc, true );
	glUniform1i( m_ditherLoc, m_dither );
	glUniform1i( m_realColorLoc, m_realColor );
	glUniform2i( m_texWindowMaskLoc, m_texWindowMaskX, m_texWindowMaskY );
	glUniform2i( m_texWindowOffsetLoc, m_texWindowOffsetX, m_texWindowOffsetY );
	glUniform1f( m_resolutionScaleLoc, static_cast<float>( m_resolutionScale ) );

	SetViewport( 0, 0, VRamWidth, VRamHeight );

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
	glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT );

	m_noAttributeVAO.Bind();

	if ( m_viewVRam )
	{
		// render entire vram to window
		m_vramViewShader.Bind();
		m_vramDrawTexture.Bind();
		glViewport( 0, 0, GetVRamTextureWidth(), GetVRamTextureHeight() );

		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	}
	else
	{
		const uint32_t targetWidth = m_targetDisplayArea.width * m_resolutionScale;
		const uint32_t targetHeight = m_targetDisplayArea.height * m_resolutionScale;
		const uint32_t srcWidth = m_vramDisplayArea.width * m_resolutionScale;
		const uint32_t srcHeight = m_vramDisplayArea.height * m_resolutionScale;

		// update target display texture size
		if ( targetWidth != (uint32_t)m_displayTexture.GetWidth() || targetHeight != (uint32_t)m_displayTexture.GetHeight() )
		{
			m_displayTexture.UpdateImage( Render::InternalFormat::RGB, targetWidth, targetHeight, Render::PixelFormat::RGB, Render::PixelType::UByte );
		}

		// clear display texture
		m_displayFramebuffer.Bind();
		glViewport( 0, 0, targetWidth, targetHeight );
		glClear( GL_COLOR_BUFFER_BIT );

		// render to display texture
		if ( m_displayEnable )
		{
			auto setDisplayAreaUniform = [&]( GLint uniform )
			{
				glUniform4i( uniform, m_vramDisplayArea.x, m_vramDisplayArea.y, m_vramDisplayArea.width, m_vramDisplayArea.height );
			};

			if ( m_colorDepth == DisplayAreaColorDepth::B24 )
			{
				m_output24bppShader.Bind();
				setDisplayAreaUniform( m_srcRect24Loc );
			}
			else
			{
				m_output16bppShader.Bind();
				setDisplayAreaUniform( m_srcRect16Loc );
			}

			m_vramDrawTexture.Bind();
			glViewport( m_targetDisplayArea.x * m_resolutionScale, m_targetDisplayArea.y * m_resolutionScale, srcWidth, srcHeight );
			glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		}
		m_displayFramebuffer.Unbind();

		// render to window
		m_displayShader.Bind();
		m_displayTexture.Bind();

		const float displayWidth = static_cast<float>( srcWidth );
		const float displayHeight = displayWidth / m_aspectRatio;

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