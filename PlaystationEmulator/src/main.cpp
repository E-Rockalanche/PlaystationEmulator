#include "CPU.h"

#include "MemoryMap.h"

#include <SDL.h>
#include <glad/glad.h>

#include <memory>
#include <iostream>

void PrintSdlError( const char* message )
{
	std::cout << message << '\n';
	std::cout << "SDL error: " << SDL_GetError() << '\n';
}

int main( int, char** )
{
	dbLog( "initializing SDL" );
	if ( SDL_Init( SDL_INIT_VIDEO ) < 0 )
	{
		PrintSdlError( "failed to initialize SDL" );
		return 1;
	}

	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 2 );

	dbLog( "creating window" );
	const auto windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
	SDL_Window* window = SDL_CreateWindow( "PSX Emulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 512, windowFlags );
	if ( !window )
	{
		PrintSdlError( "failed to create window" );
		return 1;
	}

	dbLog( "creating OpenGL context" );
	auto glContext = SDL_GL_CreateContext( window );
	if ( !glContext )
	{
		PrintSdlError( "failed to create OpenGL context" );
		return 1;
	}

	dbLog( "loading OpenGL procedures" );
	gladLoadGLLoader( SDL_GL_GetProcAddress );

	std::cout << "vendor: " << glGetString( GL_VENDOR ) << '\n';
	std::cout << "renderer: " << glGetString( GL_VENDOR ) << '\n';
	std::cout << "version: " << glGetString( GL_VENDOR ) << '\n';

	glClearColor( 0, 0, 0, 1 );
	glClear( GL_COLOR_BUFFER_BIT );

	SDL_GL_SwapWindow( window );

	auto bios = std::make_unique<PSX::Bios>();
	if ( !PSX::LoadBios( "bios.bin", *bios ) )
	{
		std::printf( "Could not load BIOS" );
		return 1;
	}

	auto ram = std::make_unique<PSX::Ram>();
	ram->Fill( char( -1 ) );

	auto scratchpad = std::make_unique<PSX::Scratchpad>();
	scratchpad->Fill( char( -1 ) );

	auto memControl = std::make_unique<PSX::MemoryControl>();

	auto gpu = std::make_unique<PSX::Gpu>();

	auto dmaRegisters = std::make_unique<PSX::Dma>( *ram, *gpu );

	PSX::MemoryMap memoryMap{ *ram, *scratchpad, *memControl, *dmaRegisters, *gpu, *bios };

	auto cpu = std::make_unique<PSX::MipsR3000Cpu>( memoryMap );

	cpu->Reset();

	while ( true )
	{
		cpu->Tick();
	}

	SDL_DestroyWindow( window );
	SDL_Quit();
	return 0;
}