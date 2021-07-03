#pragma once

#include <Render/VertexArrayObject.h>
#include <Render/Buffer.h>
#include <Render/Shader.h>
#include <Render/Texture.h>

#include <Render/Error.h> // temp

#include <stdx/assert.h>

#include <SDL.h>

#include <cstdint>
#include <vector>

namespace PSX
{

struct Position
{
	Position() = default;

	Position( int16_t x_, int16_t y_ ) : x{ x_ }, y{ y_ } {}

	explicit Position( uint32_t gpuParam )
		: x{ static_cast<int16_t>( gpuParam ) }
		, y{ static_cast<int16_t>( gpuParam >> 16 ) }
	{}

	int16_t x;
	int16_t y;
};

struct Color
{
	Color() = default;

	Color( uint8_t r_, uint8_t g_, uint8_t b_ ) : r{ r_ }, g{ g_ }, b{ b_ } {}

	explicit Color( uint32_t gpuParam )
		: r{ static_cast<uint8_t>( gpuParam ) }
		, g{ static_cast<uint8_t>( gpuParam >> 8 ) }
		, b{ static_cast<uint8_t>( gpuParam >> 16 ) }
	{}

	uint8_t r;
	uint8_t g;
	uint8_t b;
};

struct TexCoord
{
	TexCoord() = default;

	TexCoord( uint16_t u_, uint16_t v_ ) : u{ u_ }, v{ v_ } {}

	// tex coords are only 8bit in gpu param
	explicit TexCoord( uint32_t gpuParam )
		: u{ static_cast<uint16_t>( gpuParam & 0xff ) }
		, v{ static_cast<uint16_t>( ( gpuParam >> 8 ) & 0xff ) }
	{}

	uint16_t u;
	uint16_t v;
};

struct Vertex
{
	Position position;
	Color color;
	TexCoord texCoord;
	uint16_t clut;
	uint16_t texPage = ( 1 << 11 ); // disable texture
};

class Renderer
{
public:
	static constexpr size_t VRamWidth16 = 1024; // shorts
	static constexpr size_t VRamWidth8 = 2048; // bytes
	static constexpr size_t VRamHeight = 512;
	static constexpr size_t VRamSize = VRamWidth8 * VRamHeight; // bytes

	bool Initialize( SDL_Window* window );

	void SetOrigin( int32_t x, int32_t y );
	void SetDisplaySize( uint32_t w, uint32_t h );

	void UploadVRam( const uint16_t* vram );

	void PushTriangle( const Vertex vertices[ 3 ] );
	void PushQuad( const Vertex vertices[ 4 ] );

	void DrawBatch();

	void RenderVRamView()
	{
		if ( !m_vramViewer )
			m_vramViewer = std::make_unique<VRamViewer>();

		// render VRAM
		SDL_SetWindowSize( m_window, VRamWidth16, VRamHeight );
		glViewport( 0, 0, VRamWidth16, VRamHeight );
		m_vramViewer->Bind();
		glActiveTexture( GL_TEXTURE0 );
		m_vramColorTables.Bind();
		glDrawArrays( GL_TRIANGLES, 0, 6 );
		dbCheckRenderErrors();

		// reset context
		m_vao.Bind();
		m_shader.Use();
		m_vertexBuffer.Bind();
		glActiveTexture( GL_TEXTURE0 );
		m_vramColorTables.Bind();
		glActiveTexture( GL_TEXTURE1 );
		m_vramTextures.Bind();
		dbCheckRenderErrors();
	}

private:
	SDL_Window* m_window = nullptr;

	Render::VertexArrayObject m_vao;
	Render::ArrayBuffer m_vertexBuffer;
	Render::Shader m_shader;

	Render::Texture2D m_vramColorTables; // vram encoded as RGBA5551 to use as CLUT
	Render::Texture2D m_vramTextures; // vram encoded as R8 to use as texture CLUT indices

	Render::UniformLocation m_originLoc = -1;
	Render::UniformLocation m_displaySizeLoc = -1;

	struct Uniform
	{
		int32_t originX = 0;
		int32_t originY = 0;

		uint32_t displayWidth = 640;
		uint32_t displayHeight = 480;
	};

	Uniform m_uniform;

	std::vector<Vertex> m_vertices;

	struct VRamViewer
	{
		VRamViewer()
		{
			m_vao = Render::VertexArrayObject::Create();
			m_vao.Bind();

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

			m_shader = Render::Shader::Compile( s_vertexShader, s_fragmentShader );
			dbAssert( m_shader.Valid() );
			m_shader.SetVertexAttribPointer( "v_pos", 2, Render::Type::Float, false, sizeof( float ) * 4, 0 );
			m_shader.SetVertexAttribPointer( "v_texCoord", 2, Render::Type::Float, false, sizeof( float ) * 4, sizeof( float ) * 2 );
			m_shader.Use();

			dbCheckRenderErrors();
		}

		void Bind()
		{
			m_vao.Bind();
			m_shader.Use();
			m_vertexBuffer.Bind();
			dbCheckRenderErrors();
		}

		Render::VertexArrayObject m_vao;
		Render::ArrayBuffer m_vertexBuffer;
		Render::Shader m_shader;
	};

	std::unique_ptr<VRamViewer> m_vramViewer;
};

}