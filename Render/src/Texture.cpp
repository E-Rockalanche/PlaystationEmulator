#include "Texture.h"

namespace Render
{

GLint GetMaxTextureSize()
{
	GLint maxSize = 0;
	glGetIntegerv( GL_MAX_TEXTURE_SIZE, &maxSize );
	return maxSize;
}

}