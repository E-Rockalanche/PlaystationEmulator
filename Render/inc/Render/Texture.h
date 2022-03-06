#pragma once

#include "Buffer.h"
#include "Error.h"

#include "glad/glad.h"

#include <type_traits>
#include <utility>

namespace Render
{

enum class TextureType : GLenum
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

enum class InternalFormat : GLenum
{
	// base internal formats
	Depth = GL_DEPTH_COMPONENT,
	DepthStencil = GL_DEPTH_STENCIL,
	Red = GL_RED,
	RG = GL_RG,
	RGB = GL_RGB,
	RGBA = GL_RGBA,

	// sized internal formats
	Depth16 = GL_DEPTH_COMPONENT16,
	Depth24 = GL_DEPTH_COMPONENT24,
	Depth32 = GL_DEPTH_COMPONENT32,
	R8 = GL_R8,
	SR8 = GL_R8_SNORM,
	R16 = GL_R16,
	SR16 = GL_R16_SNORM,
	RG8 = GL_RG8,
	SRG8 = GL_RG8_SNORM,
	RG16 = GL_RG16,
	SRG16 = GL_RG16_SNORM,
	R3_G3_B2 = GL_R3_G3_B2,
	RGB4 = GL_RGB4,
	RGB5 = GL_RGB5,
	RGB8 = GL_RGB8,
	SRGB8 = GL_RGB8_SNORM,
	RGB10 = GL_RGB10,
	RGB12 = GL_RGB12,
	SRGB16 = GL_RGB16_SNORM,
	RGBA2 = GL_RGBA2,
	RGBA4 = GL_RGBA4,
	RGB5_A1 = GL_RGB5_A1,
	RGBA8 = GL_RGBA8,
	SRGBA8 = GL_RGBA8_SNORM,
	RGB10_A2 = GL_RGB10_A2,
	RGB10_A2UI = GL_RGB10_A2UI,
	RGBA12 = GL_RGBA12,
	RGBA16 = GL_RGBA16,
	/*
	TODO
	GL_SRGB8,
	GL_SRGB8_ALPHA8,
	GL_R16F,
	GL_RG16F,
	GL_RGB16F,
	GL_RGBA16F,
	GL_R32F,
	GL_RG32F,
	GL_RGB32F,
	GL_RGBA32F,
	GL_R11F_G11F_B10F,
	GL_RGB9_E5,
	GL_R8I,
	GL_R8UI,
	GL_R16I,
	*/
	R16UI = GL_R16UI,
	/*
	GL_R32I,
	GL_R32UI,
	GL_RG8I,
	GL_RG8UI,
	GL_RG16I,
	GL_RG16UI,
	GL_RG32I,
	GL_RG32UI,
	GL_RGB8I,
	GL_RGB8UI,
	GL_RGB16I,
	GL_RGB16UI,
	GL_RGB32I,
	GL_RGB32UI,
	GL_RGBA8I,
	GL_RGBA8UI,
	GL_RGBA16I,
	GL_RGBA16UI,
	GL_RGBA32I,
	GL_RGBA32UI,
	*/

	// compressed formats
	CompressedRed = GL_COMPRESSED_RED,
	CompressedRG = GL_COMPRESSED_RG,
	CompressedRGB = GL_COMPRESSED_RGB,
	CompressedRGBA = GL_COMPRESSED_RGBA,
	CompressedSRGB = GL_COMPRESSED_SRGB,
	CompressedSRGBA = GL_COMPRESSED_SRGB_ALPHA,
	// TODO
};

enum class PixelFormat : GLenum
{
	Red = GL_RED,
	RG = GL_RG,
	RGB = GL_RGB,
	BGR = GL_BGR,
	RGBA = GL_RGBA,
	BGRA = GL_BGRA,
	Red_Int = GL_RED_INTEGER,
	RG_Int = GL_RG_INTEGER,
	RGB_Int = GL_RGB_INTEGER,
	BGR_Int = GL_BGR_INTEGER,
	RGBA_Int = GL_RGBA_INTEGER,
	BGRA_Int = GL_BGRA_INTEGER,
	StencilIndex = GL_STENCIL_INDEX,
	Depth = GL_DEPTH_COMPONENT,
	DepthStencil = GL_DEPTH_STENCIL
};

enum class PixelType : GLenum
{
	UByte = GL_UNSIGNED_BYTE,
	Byte = GL_BYTE,
	UShort = GL_UNSIGNED_SHORT,
	Short = GL_SHORT,
	UInt = GL_UNSIGNED_INT,
	Int = GL_INT,
	HalfFloat = GL_HALF_FLOAT,
	Float = GL_FLOAT,
	UByte_3_3_2 = GL_UNSIGNED_BYTE_3_3_2,
	UByte_2_3_3_Rev = GL_UNSIGNED_BYTE_2_3_3_REV,
	UShort_5_6_5 = GL_UNSIGNED_SHORT_5_6_5,
	UShort_5_6_5_Rev = GL_UNSIGNED_SHORT_5_6_5_REV,
	UShort_4_4_4_4 = GL_UNSIGNED_SHORT_4_4_4_4,
	UShort_4_4_4_4_Rev = GL_UNSIGNED_SHORT_4_4_4_4_REV,
	UShort_5_5_5_1 = GL_UNSIGNED_SHORT_5_5_5_1,
	UShort_1_5_5_5_Rev = GL_UNSIGNED_SHORT_1_5_5_5_REV,
	UInt_8_8_8_8 = GL_UNSIGNED_INT_8_8_8_8,
	UInt_8_8_8_8_Rev = GL_UNSIGNED_INT_8_8_8_8_REV,
	UInt_10_10_10_2 = GL_UNSIGNED_INT_10_10_10_2,
	UInt_2_10_10_10_Rev = GL_UNSIGNED_INT_2_10_10_10_REV
};

GLint GetMaxTextureSize();

namespace Detail
{

template <TextureType Type>
class Texture
{
public:
	Texture() noexcept = default;

	Texture( const Texture& ) = delete;

	Texture( Texture&& other ) noexcept : m_texture{ std::exchange( other.m_texture, 0 ) } {}

	~Texture()
	{
		Reset();
	}

	Texture& operator=( const Texture& ) = delete;

	Texture& operator=( Texture&& other ) noexcept
	{
		Reset();
		m_texture = std::exchange( other.m_texture, 0 );
		return *this;
	}

	void Reset()
	{
		if ( m_texture != 0 )
		{
			if ( m_texture == s_bound )
				Bind( 0 );

			glDeleteTextures( 1, &m_texture );
			m_texture = 0;
		}
	}

	void Bind() const
	{
		dbExpects( m_texture != 0 );
		if ( m_texture != s_bound )
			Bind( m_texture );
	}

	static void Unbind()
	{
		if ( s_bound != 0 )
			Bind( 0 );
	}

	template <typename T>
	void SetParamater( GLenum name, T value )
	{
		Bind();
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

		dbCheckRenderErrors();
	}

protected:
	static GLenum GetType() noexcept
	{
		return static_cast<GLenum>( Type );
	}

	static void Bind( GLuint texture )
	{
		glBindTexture( static_cast<GLenum>( Type ), texture );
		s_bound = texture;
	}

protected:
	GLuint m_texture = 0;

	static inline GLuint s_bound = 0;
};

}

class Texture2D : public Detail::Texture<TextureType::Texture2D>
{
	using Parent = Detail::Texture<TextureType::Texture2D>;

	friend class Framebuffer;

public:

	Texture2D() noexcept = default;

	Texture2D( Texture2D&& other ) noexcept
		: Parent( std::move( other ) )
		, m_width{ std::exchange( other.m_width, 0 ) }
		, m_height{ std::exchange( other.m_height, 0 ) }
	{}

	Texture2D& operator=( Texture2D&& other ) noexcept
	{
		Parent::operator=( std::move( other ) );
		m_width = std::exchange( other.m_width, 0 );
		m_height = std::exchange( other.m_height, 0 );
		return *this;
	}

	static Texture2D Create()
	{
		Texture2D texture;
		glGenTextures( 1, &texture.m_texture );
		texture.Bind();
		texture.SetLinearFilering( false );
		texture.SetTextureWrap( false );
		dbCheckRenderErrors();
		return texture;
	}

	static Texture2D Create( InternalFormat internalColorFormat, GLsizei width, GLsizei height, PixelFormat pixelFormat, PixelType pixelType, const void* pixels = nullptr, GLint mipmapLevel = 0 )
	{
		Texture2D texture = Create();
		texture.UpdateImage( internalColorFormat, width, height, pixelFormat, pixelType, pixels, mipmapLevel );
		return texture;
	}

	// slowest update, recreates internal data structures
	void UpdateImage( InternalFormat internalColorFormat, GLsizei width, GLsizei height, PixelFormat pixelFormat, PixelType pixelType, const void* pixels = nullptr, GLint mipmapLevel = 0 )
	{
		dbExpects( width > 0 );
		dbExpects( height > 0 );
		dbExpects( width < GetMaxTextureSize() );
		dbExpects( height < GetMaxTextureSize() );

		Bind();
		glTexImage2D( GetType(), mipmapLevel, static_cast<GLenum>( internalColorFormat ), width, height, 0, static_cast<GLenum>( pixelFormat ), static_cast<GLenum>( pixelType ), pixels );
		dbCheckRenderErrors();
		m_width = width;
		m_height = height;
	}

	// faster update, but can't change size or internal format
	void SubImage( GLint x, GLint y, GLsizei width, GLsizei height, PixelFormat pixelFormat, PixelType pixelType, const void* pixels, GLint mipmapLevel = 0 )
	{
		Bind();
		glTexSubImage2D( GetType(), mipmapLevel, x, y, width, height, static_cast<GLenum>( pixelFormat ), static_cast<GLenum>( pixelType ), pixels );
		dbCheckRenderErrors();
	}

	// render-to-texture with FBO - update entirely on GPU, very fast

	// use PBO for fast uploads from CPU

	void Reset()
	{
		Parent::Reset();
		m_width = 0;
		m_height = 0;
	}

	void SetLinearFilering( bool linear )
	{
		const auto value = linear ? GL_LINEAR : GL_NEAREST;
		SetParamater( GL_TEXTURE_MIN_FILTER, value );
		SetParamater( GL_TEXTURE_MAG_FILTER, value );
	}

	void SetTextureWrap( bool wrap )
	{
		const auto value = wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE;
		SetParamater( GL_TEXTURE_WRAP_S, value );
		SetParamater( GL_TEXTURE_WRAP_T, value );
	}

	GLsizei GetWidth() const noexcept { return m_width; }
	GLsizei GetHeight() const noexcept { return m_height; }

private:
	GLsizei m_width = 0;
	GLsizei m_height = 0;
};

}