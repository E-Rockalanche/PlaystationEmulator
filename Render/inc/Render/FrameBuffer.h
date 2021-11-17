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

enum class FramebufferStatus : GLenum
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

enum class FramebufferBinding : GLenum
{
	Read = GL_READ_FRAMEBUFFER,
	Draw = GL_DRAW_FRAMEBUFFER,
	ReadAndDraw = GL_FRAMEBUFFER
};

class Framebuffer
{
public:
	Framebuffer() noexcept = default;

	Framebuffer( const Framebuffer& ) = delete;

	Framebuffer( Framebuffer&& other ) noexcept : m_frameBuffer{ other.m_frameBuffer }
	{
		other.m_frameBuffer = 0;
	}

	~Framebuffer()
	{
		Reset();
	}

	Framebuffer& operator=( const Framebuffer& ) = delete;

	Framebuffer& operator=( Framebuffer&& other ) noexcept
	{
		Reset();
		m_frameBuffer = other.m_frameBuffer;
		other.m_frameBuffer = 0;
		return *this;
	}

	static Framebuffer Create()
	{
		GLuint frameBuffer = 0;
		glGenFramebuffers( 1, &frameBuffer );
		return Framebuffer( frameBuffer );
	}

	// returns true if complete
	FramebufferStatus AttachTexture( AttachmentType type, Texture2D& texture, GLint mipmapLevel = 0 )
	{
		Bind();
		glFramebufferTexture2D( GL_FRAMEBUFFER, static_cast<GLenum>( type ), GL_TEXTURE_2D, texture.m_texture, mipmapLevel );
		dbCheckRenderErrors();
		return static_cast<FramebufferStatus>( glCheckFramebufferStatus( GL_FRAMEBUFFER ) );
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

	void Reset();

	void Bind( FramebufferBinding binding = FramebufferBinding::ReadAndDraw ) const
	{
		dbExpects( m_frameBuffer != 0 );
		BindImp( binding, m_frameBuffer );
	}

	void Unbind() const
	{
		UnbindImp( m_frameBuffer );
	}

	static void Unbind( FramebufferBinding binding )
	{
		BindImp( binding, 0 );
	}

private:
	Framebuffer( GLuint frameBuffer ) noexcept : m_frameBuffer{ frameBuffer } {}

	static void BindImp( FramebufferBinding binding, GLuint frameBuffer );
	static void UnbindImp( GLuint frameBuffer );

private:
	GLuint m_frameBuffer = 0;

	static inline GLuint s_boundRead = 0;
	static inline GLuint s_boundDraw = 0;
};

}