#include "Shader.h"

namespace Render
{

namespace
{
constexpr GLsizei ShaderLogSize = 512;
}

void Shader::Reset()
{
	if ( m_program != 0 )
	{
		if ( m_program == s_bound )
			Bind( 0 );

		glDeleteProgram( m_program );
		m_program = 0;
	}
}

GLuint Shader::Compile( const char* source, ShaderType type )
{
	auto shader = glCreateShader( static_cast<GLuint>( type ) );
	glShaderSource( shader, 1, &source, nullptr );
	glCompileShader( shader );

	int success = 0;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &success );
	if ( !success )
	{
		char logs[ ShaderLogSize ];
		glGetShaderInfoLog( shader, ShaderLogSize, nullptr, logs );
		std::cout << "Failed to compile shader\n" << logs << std::endl;

		glDeleteShader( shader );
		return 0;
	}

	return shader;
}

Shader Shader::Link( GLuint vertexShader, GLuint fragmentShader )
{
	if ( vertexShader == 0 || fragmentShader == 0 )
	{
		dbLogError( "Shader::Link() -- Invalid arguments" );
		return Shader();
	}

	const GLuint program = glCreateProgram();
	glAttachShader( program, vertexShader );
	glAttachShader( program, fragmentShader );
	glLinkProgram( program );

	int success = 0;
	glGetProgramiv( program, GL_LINK_STATUS, &success );
	if ( !success )
	{
		char logs[ ShaderLogSize ];
		glGetProgramInfoLog( program, ShaderLogSize, nullptr, logs );
		std::cout << "Failed to link shader program\n" << logs << std::endl;
		glDeleteProgram( program );
		return Shader();
	}

	return Shader( program );
}

Shader Shader::Compile( const char* vertexSource, const char* fragmentSource )
{
	auto vertexShader = Compile( vertexSource, ShaderType::Vertex );
	auto fragmentShader = Compile( fragmentSource, ShaderType::Fragment );

	Shader shader = Link( vertexShader, fragmentShader );

	glDeleteShader( vertexShader );
	glDeleteShader( fragmentShader );

	return shader;
}

}