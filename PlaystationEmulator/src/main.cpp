#include "BIOS.h"
#include "CDRomDrive.h"
#include "CPU.h"
#include "CycleScheduler.h"
#include "DMA.h"
#include "File.h"
#include "GPU.h"
#include "InterruptControl.h"
#include "MemoryControl.h"
#include "MemoryMap.h"
#include "ControllerPorts.h"
#include "RAM.h"
#include "Renderer.h"
#include "Timers.h"

#include <Render/Error.h>

#include <SDL.h>
#include <glad/glad.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>

void PrintSdlError( const char* message )
{
	std::cout << message << '\n';
	std::cout << "SDL error: " << SDL_GetError() << '\n';
}

bool LoadExecutable( const char* filename, PSX::MipsR3000Cpu& cpu, PSX::Ram& ram )
{
	std::ifstream fin( filename, std::ios::binary );
	if ( !fin.is_open() )
	{
		dbBreakMessage( "failed to open executable file" );
		return false;
	}

	fin.seekg( 0, std::ios_base::end );
	const size_t fileSize = static_cast<size_t>( fin.tellg() );
	fin.seekg( 0, std::ios_base::beg );

	if ( ( fileSize / 0x800 < 2 ) || ( fileSize % 0x800 != 0 ) )
	{
		dbLogError( "file size must be a multiple of 0x800 greater than 1 [%x]", static_cast<uint32_t>( fileSize ) );
		return false;
	}

	PSX::ExeHeader header;
	fin.read( reinterpret_cast<char*>( &header ), sizeof( header ) );

	if ( header.id != PSX::ExeHeader::Id )
	{
		header.id[ 0xf ] = '\0';
		dbLogError( "header ID is invalid [%s]", header.id );
		return false;
	}

	if ( header.fileSize > fileSize - sizeof( header ) )
	{
		dbLogError( "header file size is greater than actual file size [%x] [%x]", header.fileSize, static_cast<uint32_t>( fileSize ) );
		return false;
	}

	const auto physicalRamDest = header.ramDestination & 0x7fffffff;

	if ( physicalRamDest + header.fileSize > PSX::Ram::Size() )
	{
		dbLogError( "file size larger than ram at destination [%x] [%x]", header.fileSize, header.ramDestination );
		return false;
	}

	// TODO: zero fill

	fin.read( (char*)ram.Data() + physicalRamDest, header.fileSize );

	cpu.DebugSetProgramCounter( header.programCounter );

	cpu.DebugSetRegister( 28, header.globalPointer );

	if ( header.stackPointerBase != 0 )
	{
		cpu.DebugSetRegister( 29, header.stackPointerBase + header.stackPointerOffset ); // TODO: is this right?
		cpu.DebugSetRegister( 30, header.stackPointerBase ); // TODO: is this right?
	}

	dbLog( "loaded %s", filename );

	return true;
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
	bios->Fill( 0xfe );
	if ( !PSX::LoadBios( "bios.bin", *bios ) )
	{
		std::cout << "could not find BIOS" << std::endl;
		return 1;
	}

	auto ram = std::make_unique<PSX::Ram>();
	ram->Fill( 0xfe );

	auto scratchpad = std::make_unique<PSX::Scratchpad>();
	scratchpad->Fill( 0xfe );

	PSX::MemoryControl memControl;
	memControl.Reset();

	PSX::InterruptControl interruptControl;
	interruptControl.Reset();

	PSX::CycleScheduler cycleScheduler;
	cycleScheduler.Reset();

	PSX::Timers timers{ interruptControl, cycleScheduler };
	timers.Reset();

	PSX::Gpu gpu{ timers, interruptControl, renderer, cycleScheduler };
	gpu.Reset();

	PSX::Dma dma{ *ram, gpu, interruptControl, cycleScheduler };
	dma.Reset();

	auto cdRomDrive = std::make_unique<PSX::CDRomDrive>( interruptControl, cycleScheduler );
	cdRomDrive->Reset();

	PSX::ControllerPorts peripheralPorts;
	peripheralPorts.Reset();

	// PSX::DualSerialPort dualSerialPort;

	PSX::MemoryMap memoryMap{ *ram, *scratchpad, memControl, peripheralPorts, interruptControl, dma, timers, *cdRomDrive, gpu, *bios };

	auto cpu = std::make_unique<PSX::MipsR3000Cpu>( memoryMap, *ram, *bios, *scratchpad, interruptControl, cycleScheduler );
	cpu->Reset();

	cycleScheduler.ScheduleNextSubscriberUpdate();

	bool viewVRam = false;

	bool quit = false;
	while ( !quit )
	{
		const uint32_t frameStart = SDL_GetTicks();

		SDL_Event event;
		while ( SDL_PollEvent( &event ) )
		{
			switch ( event.type )
			{
				case SDL_QUIT:
					quit = true;
					break;

				case SDL_KEYDOWN:
					switch ( event.key.keysym.sym )
					{
						case SDLK_t:
							// load test EXE
							LoadExecutable( "psxtest_cpu.exe", *cpu, *ram );
							break;

						case SDLK_v:
							viewVRam = !viewVRam;
							break;

						case SDLK_k:
							cpu->EnableKernelLogging = !cpu->EnableKernelLogging;
							break;

						case SDLK_c:
							cpu->EnableCpuLogging = !cpu->EnableCpuLogging;
							break;
					}
					break;
			}
		}

		// glClearColor( 1, 0, 1, 1 );
		// glClear( GL_COLOR_BUFFER_BIT );

		while ( !gpu.GetDisplayFrame() )
			cpu->Tick();

		renderer.DrawBatch();

		/*
		if( viewVRam )
			renderer.RenderVRamView();
			*/

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