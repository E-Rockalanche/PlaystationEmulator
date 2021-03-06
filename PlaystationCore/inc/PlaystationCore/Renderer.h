#pragma once

#include "GpuDefs.h"

#include "VRamCopyShader.h"

#include <Render/VertexArrayObject.h>
#include <Render/Buffer.h>
#include <Render/FrameBuffer.h>
#include <Render/Shader.h>
#include <Render/Texture.h>

#include <Math/Rectangle.h>

#include <stdx/assert.h>

#include <SDL.h>

#include <cstdint>
#include <vector>

namespace PSX
{

struct Surface
{
	std::unique_ptr<char[]> pixels;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t depth = 0;
	uint32_t pitch = 0;
	uint32_t rmask = 0;
	uint32_t gmask = 0;
	uint32_t bmask = 0;
	uint32_t amask = 0;
};

class Renderer
{
public:
	struct DisplayArea
	{
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t width = 0;
		uint32_t height = 0;
	};

public:
	bool Initialize( SDL_Window* window );

	void Reset();

	void EnableVRamView( bool enable );
	bool IsVRamViewEnabled() const { return m_viewVRam; }

	void SetTextureWindow( uint32_t maskX, uint32_t maskY, uint32_t offsetX, uint32_t offsetY );
	void SetDrawArea( GLint left, GLint top, GLint right, GLint bottom );
	void SetSemiTransparencyMode( SemiTransparencyMode semiTransparencyMode );
	void SetMaskBits( bool setMask, bool checkMask );
	void SetDrawMode( TexPage texPage, ClutAttribute clut, bool dither );

	void SetColorDepth( DisplayAreaColorDepth colorDepth )
	{
		m_colorDepth = colorDepth;
	}

	void SetDisplayEnable( bool enable )
	{
		m_displayEnable = enable;
	}

	bool GetRealColor() const { return m_realColor; }
	void SetRealColor( bool realColor );

	void SetDisplayArea( const DisplayArea& vramDisplayArea, const DisplayArea& targetDisplayArea, float aspectRatio );

	// update vram with pixel buffer
	void UpdateVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, const uint16_t* pixels );

	// read entire vram from frame buffer
	void ReadVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t hieght, uint16_t* vram );

	void FillVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b );

	void CopyVRam( uint32_t srcX, uint32_t srcY, uint32_t destX, uint32_t destY, uint32_t width, uint32_t height );

	void PushTriangle( Vertex vertices[ 3 ], bool semiTransparent );
	void PushQuad( Vertex vertices[ 4 ], bool semiTransparent );

	void DisplayFrame();

	uint32_t GetResolutionScale() const noexcept { return m_resolutionScale; }
	bool SetResolutionScale( uint32_t scale );

	uint32_t GetTargetTextureWidth() const noexcept { return m_targetDisplayArea.width * m_resolutionScale; }
	uint32_t GetTargetTextureHeight() const noexcept { return static_cast<uint32_t>( GetTargetTextureWidth() / m_aspectRatio ); }

	Surface ReadDisplayTexture();

private:
	using DepthType = int16_t;
	static constexpr DepthType MaxDepth = std::numeric_limits<DepthType>::max();
	static constexpr DepthType ResetDepth = 1;

	using Rect = Math::Rectangle<int32_t>;

private:
	void InitializeVRamFramebuffers();

	// update read texture with dirty area of draw texture
	void UpdateReadTexture();

	void RestoreRenderState();

	void ResetDirtyArea() noexcept
	{
		m_dirtyArea.left = VRamWidth;
		m_dirtyArea.top = VRamHeight;
		m_dirtyArea.right = 0;
		m_dirtyArea.bottom = 0;
	}

	void UpdateScissorRect();
	void UpdateBlendMode();
	void UpdateMaskBits();

	void EnableSemiTransparency( bool enabled );

	void DrawBatch();

	void ResetDepthBuffer();

	void UpdateCurrentDepth();

	float GetNormalizedDepth() const noexcept
	{
		return static_cast<float>( m_currentDepth ) / static_cast<float>( MaxDepth );
	}

	bool IsDrawAreaValid() const
	{
		return m_drawArea.left <= m_drawArea.right && m_drawArea.top <= m_drawArea.bottom;
	}

	static constexpr Rect GetWrappedBounds( uint32_t left, uint32_t top, uint32_t width, uint32_t height ) noexcept;

	void GrowDirtyArea( const Rect& bounds ) noexcept;

	bool UsingTexture() const noexcept	{ return !m_texPage.textureDisable; }
	bool UsingClut() const noexcept		{ return m_texPage.texturePageColors < 2; }

	bool IntersectsTextureData( const Rect& bounds )
	{
		return UsingTexture() && ( m_textureArea.Intersects( bounds ) || ( UsingClut() && m_clutArea.Intersects( bounds ) ) );
	}

	uint32_t GetVRamTextureWidth() const noexcept { return VRamWidth * m_resolutionScale; }
	uint32_t GetVRamTextureHeight() const noexcept { return VRamHeight * m_resolutionScale; }

	void SetViewport( uint32_t left, uint32_t top, uint32_t width, uint32_t height );
	void SetScissor( uint32_t left, uint32_t top, uint32_t width, uint32_t height );

private:
	SDL_Window* m_window = nullptr;

	Render::Texture2D m_vramDrawTexture;
	Render::Texture2D m_vramDrawDepthBuffer;
	Render::Framebuffer m_vramDrawFramebuffer;

	Render::Texture2D m_vramReadTexture;
	Render::Framebuffer m_vramReadFramebuffer;

	Render::Texture2D m_vramTransferTexture;
	Render::Framebuffer m_vramTransferFramebuffer;

	Render::Texture2D m_displayTexture;
	Render::Framebuffer m_displayFramebuffer;

	Render::VertexArrayObject m_noAttributeVAO;
	Render::VertexArrayObject m_vramDrawVAO;

	Render::ArrayBuffer m_vertexBuffer;

	Render::Shader m_clutShader;
	GLint m_srcBlendLoc = -1;
	GLint m_destBlendLoc = -1;
	GLint m_setMaskBitLoc = -1;
	GLint m_drawOpaquePixelsLoc = -1;
	GLint m_drawTransparentPixelsLoc = -1;
	GLint m_ditherLoc = -1;
	GLint m_realColorLoc = -1;
	GLint m_texWindowMaskLoc = -1;
	GLint m_texWindowOffsetLoc = -1;
	GLint m_resolutionScaleLoc = -1;

	Render::Shader m_vramViewShader;

	Render::Shader m_output24bppShader;
	GLint m_srcRect24Loc = -1;

	Render::Shader m_output16bppShader;
	GLint m_srcRect16Loc = -1;

	VRamCopyShader m_vramCopyShader;

	Render::Shader m_resetDepthShader;

	Render::Shader m_displayShader;

	DisplayArea m_vramDisplayArea;
	DisplayArea m_targetDisplayArea;
	float m_aspectRatio = 0.0f;

	Math::Rectangle<GLint> m_drawArea; // scissor rect

	DisplayAreaColorDepth m_colorDepth = DisplayAreaColorDepth::B15;

	SemiTransparencyMode m_semiTransparencyMode = SemiTransparencyMode::Blend;
	bool m_semiTransparencyEnabled = false;

	bool m_forceMaskBit = false;
	bool m_checkMaskBit = false;
	bool m_dither = false;
	bool m_displayEnable = false;

	TexPage m_texPage;
	ClutAttribute m_clut;

	int32_t m_texturePageX = 0;
	int32_t m_texturePageY = 0;

	uint32_t m_texWindowMaskX = 0;
	uint32_t m_texWindowMaskY = 0;
	uint32_t m_texWindowOffsetX = 0;
	uint32_t m_texWindowOffsetY = 0;

	std::vector<Vertex> m_vertices;

	Rect m_dirtyArea;
	Rect m_textureArea;
	Rect m_clutArea;

	// vertex depth to use when mask bit of pixel is set
	DepthType m_currentDepth = 0;

	// not serialized
	uint32_t m_resolutionScale = 1;
	int m_cachedWindowWidth = 0;
	int m_cachedWindowHeight = 0;
	bool m_stretchToFit = true;
	bool m_viewVRam = false;
	bool m_realColor = false;

};

}