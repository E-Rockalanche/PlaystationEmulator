#pragma once

#include "glad/glad.h"

#include <type_traits>

namespace Render
{

enum class TextureType
{
	Texture1D = GL_TEXTURE_1D,
	Texture2D = GL_TEXTURE_2D,
	Texture3D = GL_TEXTURE_3D,
	Texture1DArray = GL_TEXTURE_1D_ARRAY,
	Texture2DArray = GL_TEXTURE_2D_ARRAY,
	Rectangle = GL_TEXTURE_RECTANGLE,
	CubeMap = GL_TEXTURE_CUBE_MAP,
	Buffer = GL_TEXTURE_BUFFER,
	Texture2DMultisample = GL_TEXTURE_2D_MULTISAMPLE,
	Texture2SMultisampleArray = GL_TEXTURE_2D_MULTISAMPLE_ARRAY
};

enum class TextureFilter
{
	// min/max filter
	Nearest = GL_NEAREST,
	Linear = GL_LINEAR,

	// min filter
	NearestMipmapNearest = GL_NEAREST_MIPMAP_NEAREST,
	LinearMipmapNearest = GL_LINEAR_MIPMAP_NEAREST,
	NearestMipmapLinear = GL_NEAREST_MIPMAP_LINEAR,
	LinearMipmapLinear = GL_LINEAR_MIPMAP_LINEAR,
};

enum class TextureWrap
{
	ClampToEdge = GL_CLAMP_TO_EDGE,
	ClampToBorder = GL_CLAMP_TO_BORDER,
	MirroredRepeat = GL_MIRRORED_REPEAT,
	Repeat = GL_REPEAT
};

namespace Detail
{

class BaseTexture
{
public:
	BaseTexture() noexcept = default;
	BaseTexture( const BaseTexture& ) = delete;
	BaseTexture( BaseTexture&& other ) noexcept : m_texture{ other.m_texture }
	{
		other.m_texture = 0;
	}

	~BaseTexture()
	{
		Destroy();
	}

	BaseTexture& operator=( const BaseTexture& ) = delete;
	BaseTexture& operator=( BaseTexture&& other ) noexcept
	{
		Destroy();
		m_texture = other.m_texture;
		other.m_texture = 0;
		return *this;
	}

	void Destroy()
	{
		glDeleteTextures( 1, &m_texture ); // silently ignores 0
	}

protected:
	GLuint m_texture = 0;
};

template <TextureType Type>
class IntermediateTexture : public BaseTexture
{
public:
	void Bind()
	{
		glBindTexture( static_cast<GLenum>( Type ), m_texture );
	}

	static void Unbind()
	{
		glBindTexture( static_cast<GLenum>( Type ), 0 );
	}

	template <typename T>
	void SetParamater( GLenum name, T value )
	{
		if constexpr ( std::is_same_v<T, GLfloat> )
			glTexParameterf( static_cast<GLenum>( Type ), name, value );
		else if constexpr ( std::is_same_v<T, GLint> )
			glTexParameteri( static_cast<GLenum>( Type ), name, value );
		else if constexpr ( std::is_same_v<T, GLfloat*> || std::is_same_v<T, const GLfloat*> )
			glTexParameterfv( static_cast<GLenum>( Type ), name, value );
		else if constexpr ( std::is_same_v<T, GLint*> || std::is_same_v<T, const GLint*> )
			glTexParameteriv( static_cast<GLenum>( Type ), name, value );
		else if constexpr ( std::is_same_v<T, GLuint*> || std::is_same_v<T, const GLuint*> )
			glTexParameterIuiv( static_cast<GLenum>( Type ), name, value );
		else
			static_assert( std::is_same_v<T, GLfloat> ); // will always be false
	}

protected:
	static GLenum GetType() noexcept
	{
		return static_cast<GLenum>( Type );
	}
};

} // namespace Detail

class Texture2D : public Detail::IntermediateTexture<TextureType::Texture2D>
{
public:
	static Texture2D Create( GLint internalColorFormat, GLsizei width, GLsizei height, GLenum colorFormat, GLenum pixelType, const void* pixels, GLint mipmapLevel = 0 )
	{
		Texture2D texture;
		glGenTextures( 1, &texture.m_texture );
		texture.Bind();
		glTexImage2D( GetType(), mipmapLevel, internalColorFormat, width, height, 0, colorFormat, pixelType, pixels );
		return texture;
	}
};

}