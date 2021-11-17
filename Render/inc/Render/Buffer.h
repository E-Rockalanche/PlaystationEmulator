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
	PixelPack = GL_PIXEL_PACK_BUFFER,
	PixelUnpack = GL_PIXEL_UNPACK_BUFFER,
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

	Buffer( const Buffer& ) = delete;

	Buffer( Buffer&& other ) noexcept : m_buffer{ other.m_buffer }
	{
		other.m_buffer = 0;
	}

	Buffer* operator=( const Buffer& ) = delete;

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

	static Buffer Create()
	{
		Buffer buffer;
		glGenBuffers( 1, &buffer.m_buffer );
		dbCheckRenderErrors();
		return buffer;
	}
	
	template <typename T>
	static Buffer Create( BufferUsage usage, GLsizei size, const T* data = nullptr )
	{
		Buffer buffer = Create();
		buffer.SetData( usage, size * sizeof( T ), data );
		dbCheckRenderErrors();
		return buffer;
	}

	void Reset()
	{
		if ( m_buffer != 0 )
		{
			if ( m_buffer == s_bound )
				Bind( 0 );

			glDeleteBuffers( 1, &m_buffer );
			m_buffer = 0;
		}
	}

	bool Valid() const noexcept
	{
		return m_buffer != 0;
	}

	void Bind() const
	{
		dbExpects( m_buffer != 0 );
		if ( m_buffer != s_bound )
			Bind( m_buffer );
	}

	static void Unbind()
	{
		if ( s_bound != 0 )
			Bind( 0 );
	}

	// reallocates buffer to new size
	template <typename T>
	void SetData( BufferUsage usage, GLsizei size, const T* data = nullptr )
	{
		Bind();
		glBufferData( static_cast<GLuint>( Type ), size * sizeof( T ), data, static_cast<GLuint>( usage ) );
		dbCheckRenderErrors();
	}

	template <typename T>
	void SubData( GLsizei size, const T* data, size_t offset = 0 )
	{
		Bind();
		glBufferSubData( static_cast<GLuint>( Type ), offset * sizeof( T ), size * sizeof( T ), data );
		dbCheckRenderErrors();
	}

	void BindBufferBase( GLuint index )
	{
		dbExpects( m_buffer != 0 );
		glBindBufferBase( static_cast<GLenum>( Type ), index, m_buffer );
	}

private:
	static void Bind( GLuint buffer )
	{
		glBindBuffer( static_cast<GLenum>( Type ), buffer );
		s_bound = buffer;
	}

private:
	GLuint m_buffer = 0;

	static inline GLuint s_bound = 0;
};

using ArrayBuffer = Buffer<BufferType::Array>;
using ElementBuffer = Buffer<BufferType::Element>;
using UniformBuffer = Buffer<BufferType::Uniform>;
using TextureBuffer = Buffer<BufferType::Texture>;
using PixelPackBuffer = Buffer<BufferType::PixelPack>;
using PixelUnpackBuffer = Buffer<BufferType::PixelUnpack>;

}