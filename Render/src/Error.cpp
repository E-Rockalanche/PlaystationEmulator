#include "Error.h"

#include <iostream>

namespace Render
{

#define ErrorCase( type ) case type: std::cerr << "OpenGL ERROR: " << #type << std::endl; break

void CheckErrors()
{
	for ( GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError() )
	{
		switch ( error )
		{
			ErrorCase( GL_INVALID_ENUM );
			ErrorCase( GL_INVALID_VALUE );
			ErrorCase( GL_INVALID_OPERATION );
			ErrorCase( GL_INVALID_FRAMEBUFFER_OPERATION );
			ErrorCase( GL_OUT_OF_MEMORY );

			default:
				std::cerr << "OpenGL ERROR: Unknown" << std::endl;
				break;
		}
		dbBreak();
	}
}

#undef ErrorCase
}