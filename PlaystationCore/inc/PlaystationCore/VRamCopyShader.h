#pragma once

#include <Render/Shader.h>

namespace PSX
{

class VRamCopyShader
{
public:
	void Initialize();

	void Use(
		float srcX, float srcY, float srcW, float srcH,
		bool setMaskBit = false );

private:
	Render::Shader m_program;
	GLint m_srcRectLoc = -1;
	GLint m_forceMaskBitLoc = -1;
};

}