#pragma once

#include "Error.h"

#include <glad/glad.h>

namespace Render
{

enum class BufferType : GLuint
{
	Array = GL_ARRAY_BUFFER,
	Element = GL_ELEMENT_ARRAY_BUFFER,
	Uniform = GL_UNIFORM_BUFFER,
	Texture = GL_TEXTURE_BUFFER,
};

enum class BufferUsage : GLuint
{
	StreamDraw = GL_STREAM_DRAW,
	StreamRead = GL_STREAM_READ,
	StreamCopy = GL_STREAM_COPY,

	StaticDraw = GL_STATIC_DRAW,
	StaticRead = GL_STATIC_READ,
	StaticCopy = GL_STATIC_COPY,

	DynamicDraw = GL_DYNAMIC_DRAW,
	DynamicRead = GL_DYNAMIC_READ,
	DynamicCopy = GL_DYNAMIC_COPY
};

template <BufferType Type>
class Buffer
{
public:
	Buffer() = default;

	static Buffer Create()
	{
		Buffer buffer;
		glGenBuffers( 1, &buffer.m_buffer );
		dbCheckRenderErrors();
		return buffer;
	}

	static Buffer Create( BufferUsage usage, GLsizei size, const void* data = nullptr )
	{
		Buffer buffer = Create();
		buffer.SetData( size, data, usage );
		dbCheckRenderErrors();
		return buffer;
	}

	Buffer( Buffer&& other ) noexcept : m_buffer{ other.m_buffer }
	{
		other.m_buffer = 0;
	}

	Buffer& operator=( Buffer&& other ) noexcept
	{
		Reset();
		m_buffer = other.m_buffer;
		other.m_buffer = 0;
		return *this;
	}

	~Buffer()
	{
		Reset();
	}

	Buffer( const Buffer& ) = delete;
	Buffer* operator=( const Buffer& ) = delete;

	void Reset()
	{
		glDeleteBuffers( 1, &m_buffer );
		dbCheckRenderErrors();
		m_buffer = 0;
	}

	bool Valid() const noexcept
	{
		return m_buffer != 0;
	}

	void Bind()
	{
		glBindBuffer( static_cast<GLuint>( Type ), m_buffer );
	}

	static void Unbind()
	{
		glBindBuffer( static_cast<GLuint>( Type ), 0 );
	}

	void SetData( BufferUsage usage, GLsizei size, const void* data = nullptr )
	{
		Bind();
		glBufferData( static_cast<GLuint>( Type ), size, data, static_cast<GLuint>( usage ) );
		dbCheckRenderErrors();
	}

	void SubData( GLintptr offset, GLsizei size, const void* data )
	{
		Bind();
		glBufferSubData( static_cast<GLuint>( Type ), offset, size, data );
		dbCheckRenderErrors();
	}

	GLuint GetRawHandle() noexcept
	{
		return m_buffer;
	}

private:
	GLuint m_buffer = 0;
};

using ArrayBuffer = Buffer<BufferType::Array>;
using ElementBuffer = Buffer<BufferType::Element>;
using UniformBuffer = Buffer<BufferType::Uniform>;
using TextureBuffer = Buffer<BufferType::Texture>;

}