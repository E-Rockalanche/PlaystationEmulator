#include "FrameBuffer.h"

namespace Render
{

void Framebuffer::Reset()
{
	if ( m_frameBuffer != 0 )
	{
		UnbindImp( m_frameBuffer );
		glDeleteFramebuffers( 1, &m_frameBuffer );
		m_frameBuffer = 0;
	}
}

void Framebuffer::BindImp( FramebufferBinding binding, GLuint frameBuffer )
{
	switch ( binding )
	{
		case FramebufferBinding::Read:
			if ( s_boundRead != frameBuffer )
			{
				glBindFramebuffer( GL_READ_FRAMEBUFFER, frameBuffer );
				s_boundRead = frameBuffer;
			}
			break;

		case FramebufferBinding::Draw:
			if ( s_boundDraw != frameBuffer )
			{
				glBindFramebuffer( GL_DRAW_FRAMEBUFFER, frameBuffer );
				s_boundDraw = frameBuffer;
			}
			break;

		case FramebufferBinding::ReadAndDraw:
			if ( s_boundDraw != frameBuffer || s_boundRead != frameBuffer )
			{
				glBindFramebuffer( GL_FRAMEBUFFER, frameBuffer );
				s_boundDraw = frameBuffer;
				s_boundRead = frameBuffer;
			}
			break;

		default:
			dbBreak();
	}
}

void Framebuffer::UnbindImp( GLuint frameBuffer )
{
	if ( frameBuffer == s_boundRead )
	{
		glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
		s_boundRead = 0;
	}

	if ( frameBuffer == s_boundDraw )
	{
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
		s_boundDraw = 0;
	}
}

}