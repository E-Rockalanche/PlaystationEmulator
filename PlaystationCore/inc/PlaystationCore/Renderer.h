#pragma once

#include "GpuDefs.h"

#include "VRamCopyShader.h"

#include <Render/VertexArrayObject.h>
#include <Render/Buffer.h>
#include <Render/FrameBuffer.h>
#include <Render/Shader.h>
#include <Render/Texture.h>

#include <Render/Error.h> // temp

#include <Math/Rectangle.h>

#include <stdx/assert.h>

#include <SDL.h>

#include <cstdint>
#include <vector>

namespace PSX
{

class Renderer
{
public:
	using Rect = Math::Rectangle<int>;

	bool Initialize( SDL_Window* window );

	void Reset();

	void EnableVRamView( bool enable );
	bool IsVRamViewEnabled() const { return m_viewVRam; }

	void SetDisplayStart( uint32_t x, uint32_t y );
	void SetDisplaySize( uint32_t w, uint32_t h );
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

	bool UsingRealColor() const { return m_realColor; }
	void SetRealColor( bool realColor );

	// update vram with pixel buffer
	void UpdateVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, const uint16_t* pixels );

	// read entire vram from frame buffer
	void ReadVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t hieght, uint16_t* vram );

	void FillVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, float r, float g, float b, float a );

	void CopyVRam( int srcX, int srcY, int destX, int destY, int width, int height );

	void PushTriangle( const Vertex vertices[ 3 ], bool semiTransparent );
	void PushQuad( const Vertex vertices[ 4 ], bool semiTransparent );

	void DisplayFrame();

private:
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
	void UpdateDepthTest();

	void EnableSemiTransparency( bool enabled );

	void DrawBatch();

private:
	SDL_Window* m_window = nullptr;

	Render::Texture2D m_vramDrawTexture;
	Render::Framebuffer m_vramDrawFramebuffer;

	Render::Texture2D m_vramReadTexture;
	Render::Framebuffer m_vramReadFramebuffer;

	Render::Texture2D m_vramTransferTexture;
	Render::Framebuffer m_vramTransferFramebuffer;

	Render::Texture2D m_depthBuffer;

	Render::VertexArrayObject m_noAttributeVAO;
	Render::VertexArrayObject m_vramDrawVAO;

	Render::ArrayBuffer m_vertexBuffer;

	Render::Shader m_clutShader;
	GLint m_srcBlendLoc = -1;
	GLint m_destBlendLoc = -1;
	GLint m_texWindowMask = -1;
	GLint m_texWindowOffset = -1;
	GLint m_drawOpaquePixelsLoc = -1;
	GLint m_drawTransparentPixelsLoc = -1;
	GLint m_ditherLoc = -1;
	GLint m_realColorLoc = -1;

	Render::Shader m_fullscreenShader;

	Render::Shader m_output24bppShader;
	GLint m_srcRect24Loc = -1;

	Render::Shader m_output16bppShader;
	GLint m_srcRect16Loc = -1;

	VRamCopyShader m_vramCopyShader;

	uint32_t m_displayX = 0;
	uint32_t m_displayY = 0;
	uint32_t m_displayWidth = 0;
	uint32_t m_displayHeight = 0;

	// scissor rect
	GLint m_drawAreaLeft = 0;
	GLint m_drawAreaTop = 0;
	GLint m_drawAreaRight = 0;
	GLint m_drawAreaBottom = 0;

	DisplayAreaColorDepth m_colorDepth = DisplayAreaColorDepth::B15;

	SemiTransparencyMode m_semiTransparencyMode = SemiTransparencyMode::Blend;
	bool m_semiTransparencyEnabled = false;

	bool m_forceMaskBit = false;
	bool m_checkMaskBit = false;

	bool m_dither = false;

	bool m_displayEnable = false;

	bool m_stretchToFit = true;
	bool m_viewVRam = false;
	bool m_realColor = false;

	TexPage m_texPage;
	ClutAttribute m_clut;

	struct Uniform
	{
		int32_t texturePageX = 0;
		int32_t texturePageY = 0;

		uint32_t texWindowMaskX = 0;
		uint32_t texWindowMaskY = 0;
		uint32_t texWindowOffsetX = 0;
		uint32_t texWindowOffsetY = 0;

		float srcBlend = 1.0f;
		float destBlend = 0.0f;
	};

	Uniform m_uniform;

	std::vector<Vertex> m_vertices;

	Rect m_dirtyArea;
};

}