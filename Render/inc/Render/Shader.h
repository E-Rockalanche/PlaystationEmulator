#pragma once

#include "Error.h"
#include "Types.h"

#include <stdx/assert.h>

#include <iostream>

namespace Render
{

enum class ShaderType : GLuint
{
	Vertex = GL_VERTEX_SHADER,
	Fragment = GL_FRAGMENT_SHADER
};

class Shader
{
public:
	Shader() = default;

	Shader( const Shader& ) = delete;

	Shader( Shader&& other ) noexcept : m_program{ other.m_program }
	{
		other.m_program = 0;
	}

	~Shader()
	{
		Reset();
	}

	Shader& operator=( const Shader& ) = delete;

	Shader& operator=( Shader&& other ) noexcept
	{
		Reset();
		m_program = other.m_program;
		other.m_program = 0;
		return *this;
	}

	static GLuint Compile( const char* source, ShaderType type );
	static Shader Compile( const char* vertexSource, const char* fragmentSource );
	static Shader Link( GLuint vertexShader, GLuint fragmentShader );

	void Reset();

	bool Valid() const noexcept
	{
		return m_program != 0;
	}

	void Bind() const
	{
		dbExpects( m_program != 0 );

		if ( m_program != s_bound )
			Bind( m_program );
	}

	static void Unbind()
	{
		if ( s_bound != 0 )
			Bind( 0 );
	}

	GLint GetAttributeLocation( const char* name )
	{
		dbExpects( m_program != 0 );
		const GLint location = glGetAttribLocation( m_program, name );
		dbAssert( location != -1 );
		return location;
	}

	GLint GetUniformLocation( const char* name )
	{
		dbExpects( m_program != 0 );
		const GLint location = glGetUniformLocation( m_program, name );
		dbAssert( location != -1 );
		return location;
	}

	void BindUniformBlock( const char* name, GLuint block )
	{
		dbExpects( m_program != 0 );
		const auto index = glGetUniformBlockIndex( m_program, name );
		glUniformBlockBinding( m_program, index, block );
	}

private:
	Shader( GLuint program ) : m_program{ program } {}

	static void Bind( GLuint program )
	{
		glUseProgram( program );
		s_bound = program;
	}

private:
	GLuint m_program = 0;

	static inline GLuint s_bound = 0;
};

}