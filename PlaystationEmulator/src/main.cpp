#include "CPU.h"

#include "MemoryMap.h"

#include "Renderer.h"

#include <Render/Error.h>

#include <SDL.h>
#include <glad/glad.h>

#include <cmath>
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
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 3 );

	dbLog( "creating window" );
	int windowWidth = 640;
	int windowHeight = 480;
	const auto windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
	SDL_Window* window = SDL_CreateWindow( "PSX Emulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowWidth, windowHeight, windowFlags );
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
	std::cout << "renderer: " << glGetString( GL_RENDERER ) << '\n';
	std::cout << "version: " << glGetString( GL_VERSION ) << '\n';

	glViewport( 0, 0, windowWidth, windowHeight );

	glClearColor( 0, 0, 0, 1 );
	glClear( GL_COLOR_BUFFER_BIT );

	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	/*
	PSX::Renderer renderer;

	const PSX::Color red{ 0x0000ffu };
	const PSX::Color green{ 0x00ff00u };
	const PSX::Color blue{ 0xff0000u };

	PSX::Vertex vertices[]
	{
		PSX::Vertex{ PSX::Position( 64, 64 ), red },
		PSX::Vertex{ PSX::Position( 256-64, 64 ), green },
		PSX::Vertex{ PSX::Position( 256-64, 224-64 ), blue }
	};

	bool quit = false;
	while ( !quit )
	{
		const auto time = SDL_GetTicks();

		SDL_Event event;
		while ( SDL_PollEvent( &event ) )
		{
			switch ( event.type )
			{
				case SDL_QUIT:
					quit = true;
					break;
			}
		}

		glClear( GL_COLOR_BUFFER_BIT );
		
		renderer.PushTriangle( vertices[ 0 ], vertices[ 1 ], vertices[ 2 ] );
		renderer.DrawBatch();

		SDL_GL_SwapWindow( window );

		Render::CheckErrors();

		SDL_Delay( 1000 / 60 );
	}
	*/

	PSX::Renderer renderer;
	
	auto bios = std::make_unique<PSX::Bios>();
	if ( !PSX::LoadBios( "bios.bin", *bios ) )
	{
		std::cout << "could not find BIOS" << std::endl;
		return 1;
	}

	auto ram = std::make_unique<PSX::Ram>();
	ram->Fill( char( -1 ) );

	auto scratchpad = std::make_unique<PSX::Scratchpad>();
	scratchpad->Fill( char( -1 ) );

	PSX::MemoryControl memControl;

	PSX::InterruptControl interruptControl;

	PSX::Timers timers{ interruptControl };

	PSX::Gpu gpu{ timers, interruptControl, renderer };

	PSX::Dma dma{ *ram, gpu };

	auto cdRomDrive = std::make_unique<PSX::CDRomDrive>( interruptControl );

	PSX::MemoryMap memoryMap{ *ram, *scratchpad, memControl, interruptControl, dma, timers, *cdRomDrive, gpu, *bios };

	auto cpu = std::make_unique<PSX::MipsR3000Cpu>( memoryMap, *scratchpad, timers, interruptControl );

	bool quit = false;
	while ( !quit )
	{
		const uint32_t frameStart = SDL_GetTicks();

		SDL_Event event;
		while ( SDL_PollEvent( &event ) )
		{
			switch ( event.type )
			{
				case SDL_QUIT:	quit = true;	break;
			}
		}

		while ( !gpu.GetDisplayFrame() )
		{
			// update target cycles
			if ( timers.NeedsUpdate() )
				timers.SetTargetCycles( std::min( timers.GetCyclesUntilIrq(), gpu.GetCpuCyclesUntilEvent() ) );

			while ( !timers.NeedsUpdate() )
				cpu->Tick();

			// increment timers
			if ( timers.GetCycles() > 0 )
			{
				gpu.UpdateTimers( timers.GetCycles() );
				timers.UpdateNow();
			}
		}

		renderer.DrawBatch();

		SDL_GL_SwapWindow( window );

		Render::CheckErrors();

		const uint32_t elapsed = SDL_GetTicks() - frameStart;

		const auto TargetMilliseconds = static_cast<uint32_t>( 1000.0f / gpu.GetRefreshRate() );
		if ( elapsed < TargetMilliseconds )
			SDL_Delay( TargetMilliseconds - elapsed );
	}
	

	SDL_GL_DeleteContext( glContext );
	SDL_DestroyWindow( window );
	SDL_Quit();
	return 0;
}