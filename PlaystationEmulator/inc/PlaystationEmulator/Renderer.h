#pragma once

#include <Render/VertexArrayObject.h>
#include <Render/Buffer.h>
#include <Render/Shader.h>

#include <cstdint>
#include <vector>

namespace PSX
{

struct Position
{
	Position( int16_t x_, int16_t y_ ) : x{ x_ }, y{ y_ } {}

	explicit Position( uint32_t value )
		: x{ static_cast<int16_t>( value ) }
		, y{ static_cast<int16_t>( value >> 16 ) }
	{}

	int16_t x;
	int16_t y;
};

struct Color
{
	Color( uint8_t r_, uint8_t g_, uint8_t b_ ) : r{ r_ }, g{ g_ }, b{ b_ } {}

	explicit Color( uint32_t value )
		: r{ static_cast<uint8_t>( value ) }
		, g{ static_cast<uint8_t>( value >> 8 ) }
		, b{ static_cast<uint8_t>( value >> 16 ) }
	{}

	uint8_t r;
	uint8_t g;
	uint8_t b;
};

struct Vertex
{
	Position position;
	Color color;
};

class Renderer
{
public:
	Renderer();

	void SetOrigin( int16_t x, int16_t y );
	void SetDisplaySize( uint16_t w, uint16_t h );
	void SetAlpha( float alpha );

	void PushTriangle( const Vertex& v1, const Vertex v2, const Vertex& v3 );

	void DrawBatch();

private:
	Render::VertexArrayObject m_vao;
	Render::ArrayBuffer m_vbo;
	Render::Shader m_shader;

	Render::UniformLocation m_originLoc = -1;
	Render::UniformLocation m_displaySizeLoc = -1;
	Render::UniformLocation m_alphaLoc = -1;

	std::vector<Vertex> m_vertices;
};

}