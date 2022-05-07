#include "App.h"

#include <PlaystationCore/AudioQueue.h>
#include <PlaystationCore/CDRom.h>
#include <PlaystationCore/ControllerPorts.h>
#include <PlaystationCore/MemoryCard.h>
#include <PlaystationCore/Renderer.h>
#include <PlaystationCore/SaveState.h>

#include <Util/CommandLine.h>
#include <Util/Stopwatch.h>

#include <ByteIO/ByteStream.h>

#include <stdx/assert.h>

#include <glad/glad.h>

#include <SDL.h>

#include <fstream>

namespace App
{

namespace
{

const char* const ExecutableExtension = ".exe";
const char* const MemoryCardExtension = ".mcr";
const char* const SaveStateExtension = ".sav";

constexpr float FpsSmoothingFactor = 0.9f;

SDL_GameController* TryOpenController( int32_t deviceIndex )
{
	if ( !SDL_IsGameController( deviceIndex ) )
	{
		LogError( "Cannot open device. Not an SDL game controller" );
		return nullptr;
	}

	SDL_GameController* controller = SDL_GameControllerOpen( deviceIndex );

	if ( controller )
		Log( "Opened SDL game controller %s", SDL_GameControllerName( controller ) );
	else
		LogError( "Failed to open SDL game controller %s", SDL_GameControllerNameForIndex( deviceIndex ) );

	return controller;
}

}

App::App() = default;
App::~App() = default;

bool App::Initialize()
{
	dbLog( "App::Initialize" );
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER ) < 0 )
	{
		LogError( "Failed to initialize SDL [%s]", SDL_GetError() );
		return false;
	}

	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 3 );

	auto& cl = Util::CommandLine::Get();
	const int winWidth = cl.GetOption( "windowwidth", 640 );
	const int winHeight = cl.GetOption( "windowheight", 480 );

	const uint32_t winFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
	m_window = SDL_CreateWindow( "PSX Emulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, winWidth, winHeight, winFlags );
	if ( m_window == nullptr )
	{
		LogError( "Failed to create SDL window [%s]", SDL_GetError() );
		return false;
	}

	m_glContext = SDL_GL_CreateContext( m_window );
	if ( m_glContext == nullptr )
	{
		LogError( "Failed to create OpenGL context [%s]", SDL_GetError() );
		return false;
	}

	if ( !gladLoadGLLoader( SDL_GL_GetProcAddress ) )
	{
		LogError( "Failed to initialize OpenGL context" );
		return false;
	}

	Log( "GL_VENDOR:   %s", glGetString( GL_VENDOR ) );
	Log( "GL_RENDERER: %s", glGetString( GL_RENDERER ) );
	Log( "GL_VERSION:  %s", glGetString( GL_VERSION ) );

	// clear default framebuffer
	glClearColor( 0, 0, 0, 1 );
	glViewport( 0, 0, winWidth, winHeight );
	glClear( GL_COLOR_BUFFER_BIT );

	// create PSX with controller
	fs::path biosFilename = cl.GetOption( "bios", fs::path{ "bios.bin" } );
	m_playstation = std::make_unique<PSX::Playstation>();
	if ( !m_playstation->Initialize( m_window, biosFilename ) )
	{
		LogError( "Failed to initialize emulator core" );
		return false;
	}

	if ( const auto romFilename = cl.FindOption( "rom" ); romFilename.has_value() )
	{
		LoadRom( *romFilename );
	}

	m_playstation->Reset();

	m_psxController = std::make_unique<PSX::Controller>();
	m_playstation->SetController( 0, m_psxController.get() );

	// try to open an SDL controller
	for ( int i = 0; i < SDL_NumJoysticks(); ++i )
	{
		m_sdlController = TryOpenController( i );
		if ( m_sdlController )
			break;
	}

	m_keyboardButtonMap = {
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

	m_controllerButtonMap = {
		{ SDL_CONTROLLER_BUTTON_BACK, PSX::Button::Select },
		{ SDL_CONTROLLER_BUTTON_LEFTSTICK, PSX::Button::L3 },
		{ SDL_CONTROLLER_BUTTON_RIGHTSTICK, PSX::Button::R3 },
		{ SDL_CONTROLLER_BUTTON_START, PSX::Button::Start },

		{ SDL_CONTROLLER_BUTTON_DPAD_UP, PSX::Button::Up },
		{ SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PSX::Button::Right },
		{ SDL_CONTROLLER_BUTTON_DPAD_DOWN, PSX::Button::Down },
		{ SDL_CONTROLLER_BUTTON_DPAD_LEFT, PSX::Button::Left },

		{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PSX::Button::L1 },
		{ SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PSX::Button::R1 },

		{ SDL_CONTROLLER_BUTTON_X, PSX::Button::Triangle },
		{ SDL_CONTROLLER_BUTTON_A, PSX::Button::Circle },
		{ SDL_CONTROLLER_BUTTON_B, PSX::Button::X },
		{ SDL_CONTROLLER_BUTTON_Y, PSX::Button::Square },
	};

	return true;
}

void App::Shutdown()
{
	if ( m_sdlController )
	{
		SDL_GameControllerClose( m_sdlController );
		m_sdlController = nullptr;
	}

	m_playstation->GetControllerPorts().SaveMemoryCardsToDisk();
	m_playstation.reset();

	SDL_GL_DeleteContext( m_glContext );
	m_glContext = nullptr;

	SDL_DestroyWindow( m_window );
	m_window = nullptr;

	SDL_Quit();
}

bool App::LoadRom( fs::path filename )
{
	const auto pathStr = filename.string();

	auto cdrom = PSX::CDRom::Open( filename );

	if ( !cdrom )
	{
		LogError( "Failed to load ROM %s", pathStr.c_str() );
		return false;
	}

	m_playstation->SetCDRom( std::move( cdrom ) );
	Log( "Loaded ROM %s", pathStr.c_str() );

	OpenMemoryCardForRom( std::move( filename ), 0 );

	m_playstation->Reset();
	m_paused = false;

	return true;
}

bool App::LoadMemoryCard( fs::path filename, size_t slot )
{
	dbExpects( slot < 2 );
	const auto pathStr = filename.string();

	auto memoryCard = PSX::MemoryCard::Load( std::move( filename ) );

	if ( !memoryCard )
	{
		LogError( "Failed to load memory card %s", pathStr.c_str() );
		return false;
	}

	m_memCards[ slot ] = std::move( memoryCard );
	m_playstation->SetMemoryCard( slot, m_memCards[ slot ].get() );

	Log( "Loaded memory card %s", pathStr.c_str() );
	return true;
}

void App::CreateMemoryCard( fs::path filename, size_t slot )
{
	const auto pathStr = filename.string();
	m_memCards[ slot ] = PSX::MemoryCard::Create( std::move( filename ) );
	m_playstation->SetMemoryCard( slot, m_memCards[ slot ].get() );
	Log( "Created memory card %s", pathStr.c_str() );
}

void App::OpenMemoryCardForRom( fs::path filename, size_t slot )
{
	filename.replace_extension( MemoryCardExtension );
	if ( !LoadMemoryCard( filename, slot ) )
		CreateMemoryCard( filename, slot );
}

bool App::SaveState( fs::path filename )
{
	const auto pathStr = filename.string();

	std::ofstream fout( filename, std::ios::binary );
	if ( !fout.is_open() )
	{
		LogError( "Cannot open %s for saving", pathStr.c_str() );
		return false;
	}

	ByteIO::ByteStream saveState;
	PSX::SaveStateSerializer serializer( PSX::SaveStateSerializer::Mode::Write, saveState );
	m_playstation->Serialize( serializer );
	fout.write( saveState.data(), saveState.size() );
	fout.close();

	Log( "Saved state to %s", pathStr.c_str() );
	return true;
}

bool App::LoadState( fs::path filename )
{
	const auto pathStr = filename.string();

	std::ifstream fin( filename, std::ios::binary );
	if ( !fin.is_open() )
	{
		LogError( "Cannot open %s for loading", pathStr.c_str() );
		return false;
	}

	fin.seekg( 0, std::ios::end );
	const auto size = static_cast<size_t>( fin.tellg() );
	fin.seekg( 0, std::ios::beg );

	std::unique_ptr<char[]> data( new char[ size ] );
	fin.read( data.get(), size );
	fin.close();

	ByteIO::ByteStream saveState( std::move( data ), size );
	PSX::SaveStateSerializer deserializer( PSX::SaveStateSerializer::Mode::Read, saveState );
	if ( !m_playstation->Serialize( deserializer ) )
	{
		m_playstation->Reset();
		m_paused = true;

		LogError( "Failed to deserialize save state from %s", pathStr.c_str() );
		return false;
	}

	Log( "Loaded save state from %s", pathStr.c_str() );
	return true;
}

void App::SetPaused( bool pause )
{
	m_paused = pause;
	Log( "paused: %i", pause );
}

void App::SetMuted( bool mute )
{
	m_muted = mute;
	m_playstation->GetAudioQueue().SetPaused( mute );
	Log( "muted: %i", mute );
}

void App::SetFullscreen( bool fullscreen )
{
	m_fullscreen = fullscreen;
	uint32_t flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
	SDL_SetWindowFullscreen( m_window, flags );
}

void App::PollEvents()
{
	SDL_Event event;
	while ( SDL_PollEvent( &event ) )
	{
		switch ( event.type )
		{
			case SDL_QUIT:
			{
				m_quitting = true;
				Log( "Quitting App..." );
				break;
			}

			case SDL_KEYDOWN:
			{
				const auto key = event.key.keysym.sym;

				if ( HandleHotkeyPress( key ) )
					break;

				auto it = m_keyboardButtonMap.find( key );
				if ( it != m_keyboardButtonMap.end() )
					m_psxController->Press( it->second );

				break;
			}

			case SDL_KEYUP:
			{
				const auto key = event.key.keysym.sym;

				auto it = m_keyboardButtonMap.find( key );
				if ( it != m_keyboardButtonMap.end() )
					m_psxController->Release( it->second );

				break;
			}

			case SDL_CONTROLLERBUTTONDOWN:
			{
				const auto button = event.cbutton.button;
				auto it = m_controllerButtonMap.find( button );
				if ( it != m_controllerButtonMap.end() )
				{
					m_psxController->Press( it->second );
				}
				else if ( button == SDL_CONTROLLER_BUTTON_GUIDE )
				{
					const bool analog = !m_psxController->GetAnalogMode();
					m_psxController->SetAnalogMode( analog );
					dbLog( "controller analog mode: %s", analog ? "true" : "false" );
				}
				break;
			}

			case SDL_CONTROLLERBUTTONUP:
			{
				const auto button = event.cbutton.button;
				auto it = m_controllerButtonMap.find( button );
				if ( it != m_controllerButtonMap.end() )
					m_psxController->Release( it->second );
				break;
			}

			case SDL_CONTROLLERAXISMOTION:
			{
				const int16_t value = event.caxis.value;

				auto scaleToUint8 = []( int16_t val )
				{
					static constexpr int16_t JoyAxisMax = 29697;
					int clamped = std::clamp<int16_t>( val, -JoyAxisMax, JoyAxisMax ) + JoyAxisMax;
					return static_cast<uint8_t>( ( clamped * 0xff ) / ( 2 * JoyAxisMax ) );
				};

				static constexpr int16_t TriggerDeadzone = std::numeric_limits<int16_t>::max() / 2;

				switch ( event.caxis.axis )
				{
					case SDL_CONTROLLER_AXIS_LEFTX:
						m_psxController->SetAxis( PSX::Axis::JoyLeftX, scaleToUint8( value ) );
						break;

					case SDL_CONTROLLER_AXIS_LEFTY:
						m_psxController->SetAxis( PSX::Axis::JoyLeftY, scaleToUint8( value ) );
						break;

					case SDL_CONTROLLER_AXIS_RIGHTX:
						m_psxController->SetAxis( PSX::Axis::JoyRightX, scaleToUint8( value ) );
						break;

					case SDL_CONTROLLER_AXIS_RIGHTY:
						m_psxController->SetAxis( PSX::Axis::JoyRightY, scaleToUint8( value ) );
						break;

					case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
					{
						if ( value <= TriggerDeadzone )
							m_psxController->Release( PSX::Button::L2 );
						else
							m_psxController->Press( PSX::Button::L2 );
						break;
					}

					case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
					{
						if ( value <= TriggerDeadzone )
							m_psxController->Release( PSX::Button::R2 );
						else
							m_psxController->Press( PSX::Button::R2 );
						break;
					}
				}
				break;
			}

			case SDL_DROPFILE:
			{
				fs::path filename = event.drop.file;
				const auto extension = filename.extension();

				if ( extension == ExecutableExtension )
				{
					m_playstation->HookExe( std::move( filename ) );
					m_playstation->Reset();
					SDL_SetWindowTitle( m_window, event.drop.file );
					m_paused = false;
					Log( "Loaded executable %s", event.drop.file );
				}
				else if ( extension == MemoryCardExtension )
				{
					LoadMemoryCard( std::move( filename ), 0 );
				}
				else if ( extension == SaveStateExtension )
				{
					LoadState( std::move( filename ) );
				}
				else if ( !LoadRom( std::move( filename ) ) )
				{
					LogError( "Cannot open %s. Unknown file type", event.drop.file );
				}
				break;
			}

			case SDL_JOYDEVICEADDED:
			{
				if ( !m_sdlController )
				{
					// open new controller
					m_sdlController = TryOpenController( event.jdevice.which );
				}
				break;
			}

			case SDL_JOYDEVICEREMOVED:
			{
				if ( m_sdlController && !SDL_GameControllerGetAttached( m_sdlController ) )
				{
					SDL_GameControllerClose( m_sdlController );
					m_sdlController = nullptr;

					// try to open a different controller
					for ( int32_t i = 0; i < SDL_NumJoysticks(); ++i )
					{
						m_sdlController = TryOpenController( i );
						if ( m_sdlController )
							break;
					}
				}
				break;
			}
		}
	}
}

fs::path App::GetQuicksaveFilename() const
{
	auto* cdrom = m_playstation->GetCDRom();
	dbAssert( cdrom );
	fs::path filename = cdrom->GetFilename();
	filename.replace_extension( SaveStateExtension );
	return filename;
}

bool App::SetResolutionScale( uint32_t scale )
{
	auto& renderer = m_playstation->GetRenderer();
	if ( !renderer.SetResolutionScale( scale ) )
	{
		LogError( "Cannot set resolution scale to x%u", scale );
		return false;
	}

	const int winWidth = renderer.GetTargetTextureWidth();
	const int winHeight = renderer.GetTargetTextureHeight();
	SDL_SetWindowSize( m_window, winWidth, winHeight );
	Log( "Set resolution scale to x%u", scale );
	return true;
}

bool App::HandleHotkeyPress( SDL_Keycode key )
{
	switch ( key )
	{
		case SDLK_F1:
			SetPaused( !m_paused );
			return true;

		case SDLK_F2:
			m_stepFrame = true;
			return true;

		case SDLK_F3:
			SetMuted( !IsMuted() );
			return true;

		case SDLK_F5:
			SaveState( GetQuicksaveFilename() );
			return true;

		case SDLK_F6:
		{
			// toggle VRAM view
			auto& renderer = m_playstation->GetRenderer();
			renderer.EnableVRamView( !renderer.IsVRamViewEnabled() );
			return true;
		}

		case SDLK_F7:
		{
			// toggle real color
			auto& renderer = m_playstation->GetRenderer();
			const bool realColor = !renderer.GetRealColor();
			renderer.SetRealColor( realColor );
			Log( "real color: %i", realColor );
			return true;
		}

		case SDLK_F9:
			LoadState( GetQuicksaveFilename() );
			return true;

		case SDLK_F11:
			SetFullscreen( !IsFullscreen() );
			return true;

		case SDLK_PLUS:
		case SDLK_EQUALS:
			SetResolutionScale( m_playstation->GetRenderer().GetResolutionScale() + 1 );
			return true;

		case SDLK_MINUS:
		case SDLK_UNDERSCORE:
			SetResolutionScale( m_playstation->GetRenderer().GetResolutionScale() - 1 );
			return true;

		case SDLK_ESCAPE:
			m_playstation->Reset();
			return true;
	}

	return false;
}

void App::Run()
{
	Util::Stopwatch stopwatch;
	stopwatch.Start();

	while ( !m_quitting )
	{
		PollEvents();

		if ( !m_paused || m_stepFrame )
		{
			m_stepFrame = false;
			m_playstation->RunFrame();
		}
		else
		{
			m_playstation->GetRenderer().DisplayFrame();
		}

		using MillisecondsD = std::chrono::duration<float, std::milli>;
		static const auto SpinDuration = MillisecondsD( 2.0 );

		const float refreshRate = m_playstation->GetRefreshRate();
		const auto targetMilliseconds = MillisecondsD( 1000.0f / refreshRate );
		const auto coreElapsed = std::chrono::duration_cast<MillisecondsD>( stopwatch.GetElapsed() );

		// limit frame rate
		if ( coreElapsed < targetMilliseconds )
		{
			if ( coreElapsed < ( targetMilliseconds - SpinDuration ) )
				std::this_thread::sleep_for( targetMilliseconds - SpinDuration - coreElapsed );

			while ( stopwatch.GetElapsed() < targetMilliseconds ) {}
		}

		// compensate for any lag from the last frame
		const auto totalElapsed = std::chrono::duration_cast<MillisecondsD>( stopwatch.GetElapsed() );
		const MillisecondsD compensation = ( totalElapsed > targetMilliseconds && totalElapsed < targetMilliseconds * 2 ) ? ( totalElapsed - targetMilliseconds ) : MillisecondsD{};
		stopwatch.Start( std::chrono::duration_cast<Util::Stopwatch::Duration>( compensation ) );

		if ( coreElapsed > targetMilliseconds )
			dbLogDebug( "target millis: %f, elapsed: %f, core elapsed: %f, compensation: %f", targetMilliseconds.count(), totalElapsed.count(), coreElapsed.count(), compensation.count() );

		// calculate FPS
		const float curFps = 1000.0f / totalElapsed.count();
		m_smoothedAverageFPS = FpsSmoothingFactor * m_smoothedAverageFPS + ( 1.0f - FpsSmoothingFactor ) * curFps;
	}
}

}