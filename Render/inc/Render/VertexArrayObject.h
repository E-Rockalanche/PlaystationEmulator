#pragma once

#include "Types.h"

#include <stdx/assert.h>
#include <glad/glad.h>

namespace Render
{

class VertexArrayObject
{
public:
	VertexArrayObject() = default;

	VertexArrayObject( const VertexArrayObject& ) = delete;

	VertexArrayObject( VertexArrayObject&& other ) noexcept : m_vao{ other.m_vao }
	{
		other.m_vao = 0;
	}

	~VertexArrayObject()
	{
		Reset();
	}

	VertexArrayObject& operator=( const VertexArrayObject& ) = delete;

	VertexArrayObject& operator=( VertexArrayObject&& other ) noexcept
	{
		Reset();
		m_vao = other.m_vao;
		other.m_vao = 0;
		return *this;
	}

	static VertexArrayObject Create()
	{
		VertexArrayObject vao;
		glGenVertexArrays( 1, &vao.m_vao );
		return vao;
	}

	bool Valid() const noexcept
	{
		return m_vao != 0;
	}

	void Reset();

	// define float, vec2, vec3, vec4 to be used in shader
	void AddFloatAttribute( GLint location, GLint size, Type type, GLboolean normalized, GLsizei stride = 0, uintptr_t offset = 0 );

	// define int, ivec2, ivec3, ivec4 to be used in shader
	void AddIntAttribute( GLint location, GLint size, Type type, GLsizei stride = 0, uintptr_t offset = 0 );

	void Bind() const
	{
		dbExpects( m_vao != 0 );
		if ( m_vao != s_bound )
			Bind( m_vao );
	}

	static void Unbind()
	{
		if ( s_bound != 0 )
			Bind( 0 );
	}

private:
	VertexArrayObject( GLuint vao ) : m_vao{ vao } {}

	static void Bind( GLuint vao )
	{
		glBindVertexArray( vao );
		s_bound = vao;
	}

private:
	GLuint m_vao = 0;

	static inline GLuint s_bound = 0;
};

}