#pragma once

#include "Defs.h"

#include <filesystem>
namespace fs = std::filesystem;

namespace PSX
{

class Playstation
{
public:
	Playstation();
	~Playstation();

	bool Initialize( SDL_Window* window, const fs::path& biosFilename );

	void Reset();

	void SetController( size_t slot, Controller* controller );
	void SetMemoryCard( size_t slot, MemoryCard* memCard );

	void RunFrame();

	bool LoadRom( const fs::path& filename );

	void HookExe( fs::path filename );

	float GetRefreshRate() const;

public:
	AudioQueue&			GetAudioQueue()			{ dbAssert( m_audioQueue); return *m_audioQueue; }
	Bios&				GetBios()				{ dbAssert( m_bios ); return *m_bios; }
	CDRomDrive&			GetCDRomDrive()			{ dbAssert( m_cdromDrive ); return *m_cdromDrive; }
	ControllerPorts&	GetControllerPorts()	{ dbAssert( m_controllerPorts ); return *m_controllerPorts; }
	EventManager&		GetEventManager()		{ dbAssert( m_eventManager ); return *m_eventManager; }
	Dma&				GetDma()				{ dbAssert( m_dma ); return *m_dma; }
	DualSerialPort*		GetDualSerialPort()		{ return m_dualSerialPort.get(); }
	Gpu&				GetGpu()				{ dbAssert( m_gpu ); return *m_gpu; }
	InterruptControl&	GetInterruptControl()	{ dbAssert( m_interruptControl ); return *m_interruptControl; }
	MemoryControl&		GetMemoryControl()		{ dbAssert( m_memoryControl ); return *m_memoryControl; }
	MemoryMap&			GetMemoryMap()			{ dbAssert( m_memoryMap ); return *m_memoryMap; }
	MipsR3000Cpu&		GetCpu()				{ dbAssert( m_cpu ); return *m_cpu; }
	Ram&				GetRam()				{ dbAssert( m_ram ); return *m_ram; }
	Renderer&			GetRenderer()			{ dbAssert( m_renderer ); return *m_renderer; }
	Scratchpad&			GetScratchpad()			{ dbAssert( m_scratchpad ); return *m_scratchpad; }
	Spu&				GetSpu()				{ dbAssert( m_spu ); return *m_spu; }
	Timers&				GetTimers()				{ dbAssert( m_timers ); return *m_timers; }

private:
	std::unique_ptr<EventManager> m_eventManager; // must be destroyed last
	std::unique_ptr<AudioQueue> m_audioQueue;
	std::unique_ptr<Bios> m_bios;
	std::unique_ptr<CDRomDrive> m_cdromDrive;
	std::unique_ptr<ControllerPorts> m_controllerPorts;
	std::unique_ptr<Dma> m_dma;
	std::unique_ptr<DualSerialPort> m_dualSerialPort; // optional
	std::unique_ptr<Gpu> m_gpu;
	std::unique_ptr<InterruptControl> m_interruptControl;
	std::unique_ptr<MacroblockDecoder> m_mdec;
	std::unique_ptr<MemoryControl> m_memoryControl;
	std::unique_ptr<MemoryMap> m_memoryMap;
	std::unique_ptr<MipsR3000Cpu> m_cpu;
	std::unique_ptr<Ram> m_ram;
	std::unique_ptr<Renderer> m_renderer;
	std::unique_ptr<Scratchpad> m_scratchpad;
	std::unique_ptr<Spu> m_spu;
	std::unique_ptr<Timers> m_timers;
};

}