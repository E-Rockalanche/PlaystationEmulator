#pragma once

#include "Texture.h"

#include <stdx/assert.h>
#include <glad/glad.h>

namespace Render
{

enum class AttachmentType : GLenum
{
	Color = GL_COLOR_ATTACHMENT0,
	Depth = GL_DEPTH_ATTACHMENT,
	Stencil = GL_STENCIL_ATTACHMENT,
	DepthStencil = GL_DEPTH_STENCIL_ATTACHMENT
};

enum class FrameBufferStatus : GLenum
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

	FrameBuffer& operator=( FrameBuffer&& other )
	{
		Reset();
		m_frameBuffer = other.m_frameBuffer;
		other.m_frameBuffer = 0;
		return *this;
	}

	static FrameBuffer Create()
	{
		GLuint frameBuffer = 0;
		glGenFramebuffers( 1, &frameBuffer );
		return FrameBuffer( frameBuffer );
	}

	// returns true if complete
	FrameBufferStatus AttachTexture( AttachmentType type, Texture2D& texture, GLint mipmapLevel = 0 )
	{
		Bind();
		glFramebufferTexture2D( GL_FRAMEBUFFER, static_cast<GLenum>( type ), GL_TEXTURE_2D, texture.m_texture, mipmapLevel );
		dbCheckRenderErrors();
		return static_cast<FrameBufferStatus>( glCheckFramebufferStatus( GL_FRAMEBUFFER ) );
	}

	bool IsComplete() const
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
		if ( m_frameBuffer != 0 )
		{
			if ( s_bound == m_frameBuffer )
				Bind( 0 );

			glDeleteFramebuffers( 1, &m_frameBuffer );
			m_frameBuffer = 0;
		}
	}

	void Bind() const
	{
		dbExpects( m_frameBuffer != 0 );
		if ( s_bound != m_frameBuffer )
			Bind( m_frameBuffer );
	}

	static void Unbind()
	{
		if ( s_bound != 0 )
			Bind( 0 );
	}

private:
	FrameBuffer( GLuint frameBuffer ) noexcept : m_frameBuffer{ frameBuffer } {}

	static void Bind( GLuint frameBuffer )
	{
		glBindFramebuffer( GL_FRAMEBUFFER, frameBuffer );
		s_bound = frameBuffer;
	}

private:
	GLuint m_frameBuffer = 0;

	static inline GLuint s_bound = 0;
};

}