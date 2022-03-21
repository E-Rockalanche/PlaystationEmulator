#pragma once

#include <PlaystationCore/Playstation.h>
#include <PlaystationCore/Controller.h>

#include <stdx/flat_unordered_map.h>

#include <SDL.h>

#include <array>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

namespace App
{

class App
{
public:
	App();
	~App();

	bool Initialize();

	void Shutdown();

	void Run();

	bool LoadRom( fs::path filename );

	bool LoadMemoryCard( fs::path filename, size_t slot );

	void CreateMemoryCard( fs::path filename, size_t slot );

	bool IsPaused() const { return m_paused; }
	void SetPaused( bool pause );

	bool IsMuted() const { return m_muted; }
	void SetMuted( bool mute );

	bool IsFullscreen() const { return m_fullscreen; }
	void SetFullscreen( bool fullscreen );

	bool SaveState( fs::path filename );
	bool LoadState( fs::path filename );

private:
	void OpenMemoryCardForRom( fs::path filename, size_t slot );

	void PollEvents();

	bool HandleHotkeyPress( SDL_Keycode key );

	fs::path GetQuicksaveFilename() const;

	bool SetResolutionScale( uint32_t scale );

private:
	SDL_Window* m_window = nullptr;
	SDL_GLContext m_glContext = nullptr;

	SDL_GameController* m_sdlController = nullptr;

	std::unique_ptr<PSX::Playstation> m_playstation;
	std::array<std::unique_ptr<PSX::MemoryCard>, 2> m_memCards;
	std::unique_ptr<PSX::Controller> m_psxController;

	stdx::flat_unordered_map<SDL_Keycode, PSX::Button> m_keyboardButtonMap;
	stdx::flat_unordered_map<uint8_t, PSX::Button> m_controllerButtonMap;

	float m_smoothedAverageFPS = 60.0f;

	bool m_paused = true;
	bool m_stepFrame = false;
	bool m_muted = false;
	bool m_fullscreen = false;
	bool m_quitting = false;
};

}