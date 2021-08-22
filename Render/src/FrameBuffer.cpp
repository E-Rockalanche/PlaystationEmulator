#include "FrameBuffer.h"

namespace Render
{

void FrameBuffer::Reset()
{
	if ( m_frameBuffer != 0 )
	{
		UnbindImp( m_frameBuffer );
		glDeleteFramebuffers( 1, &m_frameBuffer );
		m_frameBuffer = 0;
	}
}

void FrameBuffer::BindImp( FrameBufferBinding binding, GLuint frameBuffer )
{
	switch ( binding )
	{
		case FrameBufferBinding::Read:
			if ( s_boundRead != frameBuffer )
			{
				glBindFramebuffer( GL_READ_FRAMEBUFFER, frameBuffer );
				s_boundRead = frameBuffer;
			}
			break;

		case FrameBufferBinding::Draw:
			if ( s_boundDraw != frameBuffer )
			{
				glBindFramebuffer( GL_DRAW_FRAMEBUFFER, frameBuffer );
				s_boundDraw = frameBuffer;
			}
			break;

		case FrameBufferBinding::ReadAndDraw:
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

void FrameBuffer::UnbindImp( GLuint frameBuffer )
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