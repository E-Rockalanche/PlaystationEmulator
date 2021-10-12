#pragma once

#include <glad/glad.h>

#include <type_traits>

namespace Render
{

enum class Type : GLenum
{
	Byte = GL_BYTE,
	UByte = GL_UNSIGNED_BYTE,
	Short = GL_SHORT,
	UShort = GL_UNSIGNED_SHORT,
	Int = GL_INT,
	UInt = GL_UNSIGNED_INT,
	Float = GL_FLOAT,
	Double = GL_DOUBLE,
};

namespace Detail
{
	template <typename T>
	struct GLTypeHelper;

	#define DEF_TYPE( type, enumValue )	\
	template<>	\
	struct GLTypeHelper<type> { static constexpr Type Value = Type::enumValue; };

	DEF_TYPE( GLbyte, Byte )
	DEF_TYPE( GLubyte, UByte )
	DEF_TYPE( GLshort, Short )
	DEF_TYPE( GLushort, UShort )
	DEF_TYPE( GLint, Int )
	DEF_TYPE( GLuint, UInt )
	DEF_TYPE( GLfloat, Float )
	DEF_TYPE( GLdouble, Double )

	#undef DEF_TYPE
}

template <typename T>
inline constexpr Type GetTypeEnum()
{
	return Detail::GLTypeHelper<std::decay_t<T>>::Value;
}

template <typename T>
inline constexpr Type GetTypeEnum( const T& )
{
	return GetTypeEnum<T>();
}

}