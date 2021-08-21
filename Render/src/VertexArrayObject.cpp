#include "VertexArrayObject.h"

namespace Render
{

void VertexArrayObject::Reset()
{
	if ( m_vao != 0 )
	{
		if ( s_bound == m_vao )
			Bind( 0 );

		glDeleteVertexArrays( 1, &m_vao );
		m_vao = 0;
	}
}

void VertexArrayObject::AddFloatAttribute( GLint location, GLint size, Type type, GLboolean normalized, GLsizei stride, uintptr_t offset )
{
	dbExpects( location >= 0 );

	Bind();
	glVertexAttribPointer( location, size, static_cast<GLenum>( type ), normalized, stride, reinterpret_cast<void*>( offset ) );
	glEnableVertexAttribArray( location );
}

void VertexArrayObject::AddIntAttribute( GLint location, GLint size, Type type, GLsizei stride, uintptr_t offset )
{
	dbExpects( location >= 0 );

	Bind();
	glVertexAttribIPointer( location, size, static_cast<GLenum>( type ), stride, reinterpret_cast<void*>( offset ) );
	glEnableVertexAttribArray( location );
}

}