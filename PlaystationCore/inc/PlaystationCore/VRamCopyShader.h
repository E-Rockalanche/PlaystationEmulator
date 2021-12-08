#pragma once

#include <Render/Shader.h>

namespace PSX
{

class VRamCopyShader
{
public:
	void Initialize();

	// set destination rect with glViewport
	void Use(
		float srcX, float srcY, float srcW, float srcH,
		bool setMaskBit = false );

	// shader must already be bound
	void SetSourceArea( float srcX, float srcY, float srcW, float srcH );

private:
	Render::Shader m_program;
	GLint m_srcRectLoc = -1;
	GLint m_forceMaskBitLoc = -1;
};

}