#pragma once

#include "GpuDefs.h"

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

	void SetOrigin( int32_t x, int32_t y );
	void SetDisplayStart( uint32_t x, uint32_t y );
	void SetDisplaySize( uint32_t w, uint32_t h );
	void SetTextureWindow( uint32_t maskX, uint32_t maskY, uint32_t offsetX, uint32_t offsetY );
	void SetDrawArea( GLint left, GLint top, GLint right, GLint bottom );
	void SetSemiTransparency( SemiTransparency semiTransparency );

	// update vram with pixel buffer
	void UpdateVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, const uint16_t* pixels );

	// read entire vram from frame buffer
	void ReadVRam( uint16_t* vram );

	void FillVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint16_t color );

	void CopyVRam( GLint srcX, GLint srcY, GLint srcWidth, GLint srcHeight, GLint destX, GLint destY, GLint destWidth, GLint destHeight );

	void PushTriangle( const Vertex vertices[ 3 ], bool semiTransparent );
	void PushQuad( const Vertex vertices[ 4 ], bool semiTransparent );

	void DrawBatch();

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

	void CheckDrawMode( uint16_t drawMode, uint16_t clut );

	void UpdateScissorRect();

	void UpdateBlendMode();

	void SetSemiTransparencyEnabled( bool enabled );

private:
	SDL_Window* m_window = nullptr;

	// VRAM texture used as render target
	Render::Texture2D m_vramDrawTexture;
	Render::FrameBuffer m_vramDrawFrameBuffer;

	// VRAM texture used for reading
	Render::Texture2D m_vramReadTexture;
	Render::FrameBuffer m_vramReadFrameBuffer;

	Render::VertexArrayObject m_vramDrawVAO;
	Render::ArrayBuffer m_vertexBuffer;
	Render::Shader m_clutShader;

	Render::VertexArrayObject m_noAttributeVAO;
	Render::Shader m_fullscreenShader;

	GLint m_originLoc = -1;
	GLint m_displaySizeLoc = -1;
	GLint m_alphaLoc = -1;
	GLint m_texWindowMask = -1;
	GLint m_texWindowOffset = -1;

	uint32_t m_displayX = 0;
	uint32_t m_displayY = 0;
	uint32_t m_displayWidth = 640;
	uint32_t m_displayHeight = 480;

	GLint m_drawAreaLeft = 0;
	GLint m_drawAreaTop = 0;
	GLint m_drawAreaRight = 0;
	GLint m_drawAreaBottom = 0;

	struct Uniform
	{
		int32_t originX = 0;
		int32_t originY = 0;

		int32_t texturePageX = 0;
		int32_t texturePageY = 0;

		uint32_t texWindowMaskX = 0;
		uint32_t texWindowMaskY = 0;
		uint32_t texWindowOffsetX = 0;
		uint32_t texWindowOffsetY = 0;

		float alpha = 1.0f;
	};

	Uniform m_uniform;

	std::vector<Vertex> m_vertices;

	Rect m_dirtyArea;
	uint16_t m_lastDrawMode = 0;
	uint16_t m_lastClut = 0;

	SemiTransparency m_semiTransparency{};
	bool m_semiTransparencyEnabled = false;
};

}