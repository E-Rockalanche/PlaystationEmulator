#pragma once

#include <stdx/assert.h>

#include <glad/glad.h>

namespace Render
{

void CheckErrors();

}

#ifdef DEBUG
	#define dbCheckRenderErrors() Render::CheckErrors()
#else
	#define dbCheckRenderErrors() do{}while(false)
#endif