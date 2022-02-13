#pragma once

#include <stdx/assert.h>

#include <glad/glad.h>

namespace Render
{

void CheckErrors();

}

#if STDX_DEBUG
	#define dbCheckRenderErrors() Render::CheckErrors()
#else
	#define dbCheckRenderErrors() do{}while(false)
#endif