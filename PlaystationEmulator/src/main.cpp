#include "BIOS.h"
#include "CDRomDrive.h"
#include "CPU.h"
#include "CycleScheduler.h"
#include "DMA.h"
#include "GPU.h"
#include "InterruptControl.h"
#include "MemoryControl.h"
#include "MemoryMap.h"
#include "PeripheralPorts.h"
#include "RAM.h"
#include "Renderer.h"
#include "SPU.h"
#include "Timers.h"

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

	dbCheckRenderErrors();

	PSX::Renderer renderer;
	if ( !renderer.Initialize( window ) )
		return -1;
	
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

	PSX::CycleScheduler cycleScheduler;

	PSX::Timers timers{ interruptControl, cycleScheduler };

	PSX::Gpu gpu{ timers, interruptControl, renderer, cycleScheduler };

	PSX::Dma dma{ *ram, gpu };

	auto cdRomDrive = std::make_unique<PSX::CDRomDrive>( interruptControl, cycleScheduler );

	PSX::PeripheralPorts peripheralPorts;

	PSX::MemoryMap memoryMap{ *ram, *scratchpad, memControl, peripheralPorts, interruptControl, dma, timers, *cdRomDrive, gpu, *bios, cycleScheduler };

	auto cpu = std::make_unique<PSX::MipsR3000Cpu>( memoryMap, *scratchpad, interruptControl, cycleScheduler );

	cycleScheduler.UpdateNow();

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

		glClearColor( 1, 0, 1, 1 );
		glClear( GL_COLOR_BUFFER_BIT );

		while ( !gpu.GetDisplayFrame() )
			cpu->Tick();

		renderer.DrawBatch();

		// renderer.RenderVRamView();

		SDL_GL_SwapWindow( window );

		dbCheckRenderErrors();

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