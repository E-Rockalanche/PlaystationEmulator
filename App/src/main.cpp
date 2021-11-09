#include <PlaystationCore/Playstation.h>
#include <PlaystationCore/Controller.h>
#include <PlaystationCore/EventManager.h>
#include <PlaystationCore/GPU.h>
#include <PlaystationCore/MemoryCard.h>
#include <PlaystationCore/Renderer.h>

#include <Render/Error.h>

#include <System/CommandLine.h>

#include <SDL.h>
#include <glad/glad.h>

#include <stdx/flat_unordered_map.h>
#include <stdx/string.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>

void PrintSdlError( const char* message )
{
	std::cout << message << '\n';
	std::cout << "SDL error: " << SDL_GetError() << '\n';
}

int main( int argc, char** argv )
{
	CommandLine::Initialize( argc, argv );

	const auto romFilename = CommandLine::Get().FindOption( "rom" );
	const auto exeFilename = CommandLine::Get().FindOption( "exe" );
	const auto memCard1Filename = CommandLine::Get().FindOption( "memcard1" );
	const auto memCard2Filename = CommandLine::Get().FindOption( "memcard2" );
	const std::string_view biosFilename = CommandLine::Get().GetOption( "bios", "bios.bin" );

	dbLog( "initializing SDL" );
	if ( SDL_Init( SDL_INIT_VIDEO ) < 0 )
	{
		PrintSdlError( "failed to initialize SDL" );
		return 1;
	}

	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 3 );

	dbLog( "creating window" );
	std::string windowTitle = "PSX Emulator";
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

	auto playstationCore = std::make_unique<PSX::Playstation>();
	if ( !playstationCore->Initialize( window, biosFilename ) )
		return 1;

	// controller mapping
	PSX::Controller controller;
	playstationCore->SetController( 0, &controller );

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

	if ( romFilename.has_value() && playstationCore->LoadRom( *romFilename ) )
		windowTitle = *romFilename;

	if ( exeFilename.has_value() )
	{
		playstationCore->HookExe( *exeFilename );
		windowTitle = *exeFilename;
	}

	// set memory cards

	std::unique_ptr<PSX::MemoryCard> memCard1;
	std::unique_ptr<PSX::MemoryCard> memCard2;

	if ( memCard1Filename.has_value() )
	{
		memCard1 = PSX::MemoryCard::Load( *memCard1Filename );
	}
	else if ( romFilename.has_value() )
	{
		fs::path saveFilename{ *romFilename };
		saveFilename.replace_extension( "save" );

		// try to load existing memory card
		memCard1 = PSX::MemoryCard::Load( saveFilename );

		// create new memory card
		if ( memCard1 == nullptr )
			memCard1 = PSX::MemoryCard::Create( std::move( saveFilename ) );
	}

	if ( memCard2Filename.has_value() )
		memCard2 = PSX::MemoryCard::Load( *memCard2Filename );

	playstationCore->SetMemoryCard( 0, memCard1.get() );
	playstationCore->SetMemoryCard( 1, memCard2.get() );

	playstationCore->Reset();

	bool quit = false;
	bool paused = false;
	bool stepFrame = false;
	bool fullscreen = false;

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
							// cpu->EnableCpuLogging = !cpu->EnableCpuLogging;
							break;

						case SDLK_F4:
							// toggle kernel logging
							// cpu->EnableKernelLogging = !cpu->EnableKernelLogging;
							break;

						case SDLK_F5:
							// save state
							break;

						case SDLK_F6:
							// toggle vram view
							playstationCore->GetRenderer().EnableVRamView( !playstationCore->GetRenderer().IsVRamViewEnabled() );
							break;

						case SDLK_F9:
							// load state
							break;

						case SDLK_F11:
						{
							fullscreen = !fullscreen;
							SDL_SetWindowFullscreen( window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0 );
							break;
						}
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
			std::snprintf( cbuf, BufferSize, "%s - FPS:%.1f", windowTitle.c_str(), avgFps );
			SDL_SetWindowTitle( window, cbuf );
		}

		static constexpr uint32_t HookAddress = 0x80030000;

		if ( !paused || stepFrame )
		{
			stepFrame = false;

			playstationCore->RunFrame();
		}
		else
		{
			playstationCore->GetRenderer().DisplayFrame();
		}

		const auto refreshRate = playstationCore->GetRefreshRate();
		const float targetMilliseconds = 1000.0f / refreshRate;

		const uint32_t curTicks = SDL_GetTicks();
		const uint32_t elapsed = curTicks - ticks;

		if ( elapsed < targetMilliseconds )
			SDL_Delay( static_cast<uint32_t>( targetMilliseconds - elapsed ) );

		ticks = curTicks;

		const float curFps = std::min<float>( 1000.0f / elapsed, refreshRate );
		avgFps = FpsSmoothing * avgFps + ( 1.0f - FpsSmoothing ) * curFps;
	}

	if ( memCard1 && memCard1->Written() )
		memCard1->Save();

	if ( memCard2 && memCard2->Written() )
		memCard2->Save();

	playstationCore.reset();

	SDL_GL_DeleteContext( glContext );
	SDL_DestroyWindow( window );
	SDL_Quit();
	return 0;
}