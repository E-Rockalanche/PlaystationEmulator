#pragma once

#include <glad/glad.h>

#include <utility>

namespace Render
{

class VertexArrayObject
{
public:
	VertexArrayObject() = default;

	VertexArrayObject( VertexArrayObject&& other ) : m_vao{ std::exchange( other.m_vao, 0 ) } {}

	VertexArrayObject& operator=( VertexArrayObject&& other )
	{
		Reset();
		m_vao = std::exchange( other.m_vao, 0 );
		return *this;
	}

	VertexArrayObject( const VertexArrayObject& ) = delete;
	VertexArrayObject& operator=( const VertexArrayObject& ) = delete;

	~VertexArrayObject()
	{
		Reset();
	}

	void Reset()
	{
		glDeleteVertexArrays( 1, &m_vao );
		m_vao = 0;
	}

	static VertexArrayObject Create()
	{
		GLuint vao = 0;
		glGenVertexArrays( 1, &vao );
		return VertexArrayObject( vao );
	}

	// calls to glEnableVertexAttribArray, glDisableVertexAttribArray, glVertexAttribPointer, and glVertexAttribPointer will modify VAO state while bound
	void Bind()
	{
		glBindVertexArray( m_vao );
	}

	static void Unbind()
	{
		glBindVertexArray( 0 );
	}

private:
	VertexArrayObject( GLuint vao ) : m_vao{ vao } {}

private:
	GLuint m_vao = 0;
};

}