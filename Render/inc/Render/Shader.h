#pragma once

#include "Types.h"

#include <iostream>
#include <utility>

namespace Render
{

using UniformLocation = GLint;
using AttributeLocation = GLint;

enum class ShaderType : GLuint
{
	Vertex = GL_VERTEX_SHADER,
	Fragment = GL_FRAGMENT_SHADER
};

class Shader
{
public:
	Shader() = default;

	Shader( Shader&& other ) : m_program{ std::exchange( other.m_program, 0 ) } {}

	Shader& operator=( Shader&& other )
	{
		Reset();
		m_program = std::exchange( other.m_program, 0 );
		return *this;
	}

	~Shader()
	{
		Reset();
	}

	Shader( const Shader& ) = delete;
	Shader& operator=( const Shader& ) = delete;

	static GLuint Compile( const char* source, ShaderType type );
	static Shader Compile( const char* vertexSource, const char* fragmentSource );
	static Shader Link( GLuint vertexShader, GLuint fragmentShader );

	void Reset()
	{
		glDeleteProgram( m_program );
		m_program = 0;
	}

	bool Valid() const
	{
		return m_program != 0;
	}

	void Use()
	{
		glUseProgram( m_program );
	}

	AttributeLocation GetAttributeLocation( const char* name )
	{
		return glGetAttribLocation( m_program, name );
	}

	UniformLocation GetUniformLocation( const char* name )
	{
		return glGetUniformLocation( m_program, name );
	}

	// also sets data in VAO if bound
	void SetVertexAttribPointer( const char* name, GLint size, Type type, GLboolean normalized, GLsizei stride, size_t offset )
	{
		const auto location = GetAttributeLocation( name );
		glVertexAttribPointer( location, size, static_cast<GLenum>( type ), normalized, stride, reinterpret_cast<void*>( offset ) );
		glEnableVertexAttribArray( location );
	}

private:
	Shader( GLuint program ) : m_program{ program } {}

private:
	GLuint m_program = 0;
};

}