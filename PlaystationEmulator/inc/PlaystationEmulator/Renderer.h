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
	void SetDisplaySize( uint32_t w, uint32_t h );
	void SetTextureWindow( uint32_t maskX, uint32_t maskY, uint32_t offsetX, uint32_t offsetY );
	void SetDrawArea( GLint left, GLint top, GLint right, GLint bottom );

	// update vram with pixel buffer
	void UpdateVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, const uint16_t* pixels );

	// read entire vram from frame buffer
	void ReadVRam( uint16_t* vram );

	void FillVRam( uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint16_t color );

	void CopyVRam( GLint srcX, GLint srcY, GLint srcWidth, GLint srcHeight, GLint destX, GLint destY, GLint destWidth, GLint destHeight );

	void PushTriangle( const Vertex vertices[ 3 ] );
	void PushQuad( const Vertex vertices[ 4 ] );

	void DrawBatch();

	void DisplayFrame();

	/*
	void RenderVRamView()
	{
		if ( !m_vramViewer )
			m_vramViewer = std::make_unique<VRamViewer>();

		// render VRAM
		SDL_SetWindowSize( m_window, VRamWidth, VRamHeight );
		glViewport( 0, 0, VRamWidth, VRamHeight );
		m_vramViewer->Bind();
		m_vramColorTables.Bind();
		glDrawArrays( GL_TRIANGLES, 0, 6 );
		dbCheckRenderErrors();

		// reset context
		m_vramDrawVAO.Bind();
		m_clutShader.Use();
		m_vertexBuffer.Bind();
		m_vramReadTexture.Bind();
		dbCheckRenderErrors();
	}
	*/

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

	// Render::Texture2D m_vramColorTables; // vram encoded as RGBA5551 to use as CLUT
	// Render::Texture2D m_vramReadTexture; // vram encoded as R16 to use as texture CLUT indices

	Render::VertexArrayObject m_noAttributeVAO;
	Render::Shader m_fullscreenShader;

	GLint m_originLoc = -1;
	GLint m_displaySizeLoc = -1;
	GLint m_texWindowMask = -1;
	GLint m_texWindowOffset = -1;

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
	};

	Uniform m_uniform;

	std::vector<Vertex> m_vertices;

	Rect m_dirtyArea;
	uint16_t m_lastDrawMode = 0;
	uint16_t m_lastClut = 0;

	/*
	struct VRamViewer
	{
		VRamViewer()
		{
			m_vramDrawVAO = Render::VertexArrayObject::Create();
			m_vramDrawVAO.Bind();

			static const float s_vertices[]
			{
				// positions		// texCoords
				-1.0f, -1.0f,		0.0f, 1.0f,
				-1.0f, 1.0f,		0.0f, 0.0f,
				1.0f, -1.0f,		1.0f, 1.0f,

				-1.0f, 1.0f,		0.0f, 0.0f,
				1.0f, -1.0f,		1.0f, 1.0f,
				1.0f, 1.0f,			1.0f, 0.0f
			};

			m_vertexBuffer = Render::ArrayBuffer::Create();
			m_vertexBuffer.SetData( Render::BufferUsage::StaticDraw, 4 * 6 * 4, s_vertices );

			static const char* s_vertexShader = R"glsl(
				#version 330 core
				in vec2 v_pos;
				in vec2 v_texCoord;
				out vec2 TexCoord;
				void main() {
					gl_Position = vec4( v_pos, -0.5, 1.0 );
					TexCoord = v_texCoord;
				}
			)glsl";

			static const char* s_fragmentShader = R"glsl(
				#version 330 core
				in vec2 TexCoord;
				out vec4 FragColor;
				uniform sampler2D sampleTexture;
				void main() {
					FragColor = vec4( texture( sampleTexture, TexCoord ).rgb, 1.0 );
				}
			)glsl";

			m_clutShader = Render::Shader::Compile( s_vertexShader, s_fragmentShader );
			dbAssert( m_clutShader.Valid() );
			m_clutShader.SetVertexAttribPointer( "v_pos", 2, Render::Type::Float, false, sizeof( float ) * 4, 0 );
			m_clutShader.SetVertexAttribPointer( "v_texCoord", 2, Render::Type::Float, false, sizeof( float ) * 4, sizeof( float ) * 2 );
			m_clutShader.Use();

			dbCheckRenderErrors();
		}

		void Bind()
		{
			m_vramDrawVAO.Bind();
			m_clutShader.Use();
			m_vertexBuffer.Bind();
			dbCheckRenderErrors();
		}

		Render::VertexArrayObject m_vramDrawVAO;
		Render::ArrayBuffer m_vertexBuffer;
		Render::Shader m_clutShader;
	};

	std::unique_ptr<VRamViewer> m_vramViewer;
	*/
};

}