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
#include "Controller.h"
#include "ControllerPorts.h"
#include "RAM.h"
#include "Renderer.h"
#include "Timers.h"

#include <Render/Error.h>

#include <SDL.h>
#include <glad/glad.h>

#include <stdx/flat_unordered_map.h>
#include <stdx/string.h>

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
		dbBreak();
		return false;
	}

	fin.seekg( 0, std::ios_base::end );
	const size_t fileSize = static_cast<size_t>( fin.tellg() );
	fin.seekg( 0, std::ios_base::beg );

	if ( ( fileSize / 0x800 < 2 ) || ( fileSize % 0x800 != 0 ) )
	{
		dbLogError( "file size must be a multiple of 0x800 greater than 1 [%x]", static_cast<uint32_t>( fileSize ) );
		dbBreak();
		return false;
	}

	PSX::ExeHeader header;
	fin.read( reinterpret_cast<char*>( &header ), sizeof( header ) );

	if ( header.id != PSX::ExeHeader::Id )
	{
		header.id[ 0xf ] = '\0';
		dbLogError( "header ID is invalid [%s]", header.id );
		dbBreak();
		return false;
	}

	if ( header.fileSize > fileSize - sizeof( header ) )
	{
		dbLogError( "header file size is greater than actual file size [%x] [%x]", header.fileSize, static_cast<uint32_t>( fileSize ) );
		dbBreak();
		return false;
	}

	const auto physicalRamDest = header.ramDestination & 0x7fffffff;

	if ( physicalRamDest + header.fileSize > PSX::Ram::Size() )
	{
		dbLogError( "file size larger than ram at destination [%x] [%x]", header.fileSize, header.ramDestination );
		dbBreak();
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

int main( int argc, char** argv )
{
	const std::string_view filename = ( argc >= 2 ) ? argv[ 1 ] : "";

	dbLog( "initializing SDL" );
	if ( SDL_Init( SDL_INIT_VIDEO ) < 0 )
	{
		PrintSdlError( "failed to initialize SDL" );
		return 1;
	}

	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 3 );

	dbLog( "creating window" );
	const std::string windowTitle = filename.empty() ? "PSX Emulator" : filename.data();
	int windowWidth = 640;
	int windowHeight = 480;
	const auto windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
	SDL_Window* window = SDL_CreateWindow( windowTitle.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowWidth, windowHeight, windowFlags );
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

	dbCheckRenderErrors();

	PSX::Renderer renderer;
	if ( !renderer.Initialize( window ) )
		return 1;
	
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

	PSX::Spu spu;

	auto cdRomDrive = std::make_unique<PSX::CDRomDrive>( interruptControl, cycleScheduler );
	cdRomDrive->Reset();

	PSX::Dma dma{ *ram, gpu, *cdRomDrive, interruptControl, cycleScheduler };
	dma.Reset();

	PSX::ControllerPorts controllerPorts{ interruptControl, cycleScheduler };
	controllerPorts.Reset();

	PSX::MemoryMap memoryMap{ *ram, *scratchpad, memControl, controllerPorts, interruptControl, dma, timers, *cdRomDrive, gpu, spu, *bios };

	auto cpu = std::make_unique<PSX::MipsR3000Cpu>( memoryMap, *ram, *bios, *scratchpad, interruptControl, cycleScheduler );
	cpu->Reset();

	// controller mapping
	PSX::Controller controller;
	controllerPorts.SetController( 0, &controller );

	stdx::flat_unordered_map<SDL_Keycode, PSX::Button> controllerMapping
	{
		{ SDLK_RSHIFT, PSX::Button::Select },
		{ SDLK_RETURN, PSX::Button::Start },
		{ SDLK_UP, PSX::Button::Up },
		{ SDLK_RIGHT, PSX::Button::Right },
		{ SDLK_DOWN, PSX::Button::Down },
		{ SDLK_LEFT, PSX::Button::Left },
		{ SDLK_v, PSX::Button::Triangle },
		{ SDLK_c, PSX::Button::Circle },
		{ SDLK_x, PSX::Button::X },
		{ SDLK_z, PSX::Button::Square },
		{ SDLK_a, PSX::Button::L2 },
		{ SDLK_s, PSX::Button::L1 },
		{ SDLK_d, PSX::Button::R1 },
		{ SDLK_f, PSX::Button::R2 },
	};

	if ( stdx::ends_with( filename, ".bin" ) )
	{
		auto cdrom = std::make_unique<PSX::CDRom>();
		if ( cdrom->Open( filename.data() ) )
			cdRomDrive->SetCDRom( std::move( cdrom ) );
	}

	cycleScheduler.ScheduleNextUpdate();

	bool hookEXE = stdx::ends_with( filename, ".exe" );

	bool quit = false;
	bool paused = false;
	bool stepFrame = false;

	float avgFps = 0.0f;
	static constexpr float FpsSmoothing = 0.9f;

	uint32_t ticks = SDL_GetTicks();

	while ( !quit )
	{
		SDL_Event event;
		while ( SDL_PollEvent( &event ) )
		{
			switch ( event.type )
			{
				case SDL_QUIT:
					quit = true;
					break;

				case SDL_KEYDOWN:
				{
					const auto key = event.key.keysym.sym;
					
					switch ( key )
					{
						case SDLK_F1:
							paused = !paused;
							break;

						case SDLK_F2:
							stepFrame = true;
							break;

						case SDLK_F3:
							// toggle trace logging
							cpu->EnableCpuLogging = !cpu->EnableCpuLogging;
							break;

						case SDLK_F4:
							// toggle kernel logging
							cpu->EnableKernelLogging = !cpu->EnableKernelLogging;
							break;

						case SDLK_F5:
							// save state
							break;

						case SDLK_F6:
							// toggle vram view
							renderer.EnableVRamView( !renderer.IsVRamViewEnabled() );
							break;

						case SDLK_F9:
							// load state
							break;
					}

					auto it = controllerMapping.find( key );
					if ( it != controllerMapping.end() )
						controller.Press( it->second );

					break;
				}

				case SDL_KEYUP:
				{
					const auto key = event.key.keysym.sym;
					auto it = controllerMapping.find( key );
					if ( it != controllerMapping.end() )
						controller.Release( it->second );
					break;
				}
			}
		}

		// add averageFps to window title
		{
			static constexpr size_t BufferSize = 1024;
			char cbuf[ BufferSize ];
			std::snprintf( cbuf, BufferSize, "%s - averageFps:%.1f", windowTitle.c_str(), avgFps );
			SDL_SetWindowTitle( window, cbuf );
		}

		static constexpr uint32_t HookAddress = 0x80030000;

		if ( !paused || stepFrame )
		{
			stepFrame = false;

			// run frame
			while ( !gpu.GetDisplayFrame() )
			{
				if ( hookEXE && cpu->GetPC() == HookAddress )
				{
					hookEXE = false;
					LoadExecutable( filename.data(), *cpu, *ram );
				}

				cpu->Tick();
			}
		}

		gpu.ResetDisplayFrame();
		renderer.DisplayFrame();

		const auto refreshRate = gpu.GetRefreshRate();
		const float targetMilliseconds = 1000.0f / refreshRate;

		const uint32_t curTicks = SDL_GetTicks();
		const uint32_t elapsed = curTicks - ticks;

		if ( elapsed < targetMilliseconds )
			SDL_Delay( static_cast<uint32_t>( targetMilliseconds - elapsed ) );

		ticks = curTicks;

		const float curFps = std::min( 1000.0f / elapsed, refreshRate );
		avgFps = FpsSmoothing * avgFps + ( 1.0f - FpsSmoothing ) * curFps;
	}

	SDL_GL_DeleteContext( glContext );
	SDL_DestroyWindow( window );
	SDL_Quit();
	return 0;
}