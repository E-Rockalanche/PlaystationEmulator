#pragma once

#include "Texture.h"

#include <stdx/assert.h>
#include <glad/glad.h>

namespace Render
{

enum class AttachmentType
{
	Color = GL_COLOR_ATTACHMENT0,
	Depth = GL_DEPTH_ATTACHMENT,
	Stencil = GL_STENCIL_ATTACHMENT,
	DepthStencil = GL_DEPTH_STENCIL_ATTACHMENT
};

enum class FrameBufferStatus
{
	Complete = GL_FRAMEBUFFER_COMPLETE,
	Undefined = GL_FRAMEBUFFER_UNDEFINED,
	IncompleteAttachment = GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
	MissingAttachment = GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT,
	IncompleteDrawBuffer = GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER,
	IncompleteReadBuffer = GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER,
	Unsupported = GL_FRAMEBUFFER_UNSUPPORTED,
	IncompleteMultisample = GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE,
	IncompleteLayerTargets = GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS
};

class FrameBuffer
{
public:
	FrameBuffer() noexcept = default;

	FrameBuffer( const FrameBuffer& ) = delete;

	FrameBuffer( FrameBuffer&& other ) noexcept : m_frameBuffer{ other.m_frameBuffer }
	{
		other.m_frameBuffer = 0;
	}

	~FrameBuffer()
	{
		Reset();
	}

	FrameBuffer& operator=( const FrameBuffer& ) = delete;

	FrameBuffer& operator=( FrameBuffer&& other ) noexcept
	{
		m_frameBuffer = other.m_frameBuffer;
		other.m_frameBuffer = 0;
		return *this;
	}

	static FrameBuffer Create( GLsizei width, GLsizei height )
	{
		GLuint frameBuffer = 0;
		glGenFramebuffers( 1, &frameBuffer );
		return FrameBuffer( frameBuffer, width, height );
	}

	// returns true if complete
	FrameBufferStatus AttachTexture( AttachmentType type, Texture2D& texture, GLint mipmapLevel = 0 )
	{
		dbExpects( texture.GetWidth() == m_width );
		dbExpects( texture.GetHeight() == m_height );

		Bind();
		glFramebufferTexture2D( GL_FRAMEBUFFER, static_cast<GLenum>( type ), GL_TEXTURE_2D, texture.GetRawHandle(), mipmapLevel );
		return static_cast<FrameBufferStatus>( glCheckFramebufferStatus( GL_FRAMEBUFFER ) );
	}

	bool Complete() noexcept
	{
		Bind();
		return glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
	}

	bool Valid() const noexcept
	{
		return m_frameBuffer != 0;
	}

	void Reset()
	{
		glDeleteFramebuffers( 1, &m_frameBuffer );
		m_frameBuffer = 0;
		m_width = 0;
		m_height = 0;
	}

	void Bind()
	{
		dbExpects( m_frameBuffer != 0 );
		glBindFramebuffer( GL_FRAMEBUFFER, m_frameBuffer );
	}

	static void Unbind()
	{
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	}

private:
	FrameBuffer( GLuint frameBuffer, GLsizei width, GLsizei height ) noexcept
		: m_frameBuffer{ frameBuffer }
		, m_width{ width }
		, m_height{ height }
	{}

private:
	GLuint m_frameBuffer = 0;
	GLsizei m_width = 0;
	GLsizei m_height = 0;
};

}