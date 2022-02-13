#include "Playstation.h"

#include "AudioQueue.h"
#include "BIOS.h"
#include "CDRom.h"
#include "CDRomDrive.h"
#include "Controller.h"
#include "ControllerPorts.h"
#include "CPU.h"
#include "DMA.h"
#include "DualSerialPort.h"
#include "EventManager.h"
#include "File.h"
#include "GPU.h"
#include "MacroblockDecoder.h"
#include "MemoryControl.h"
#include "RAM.h"
#include "Renderer.h"
#include "SPU.h"
#include "Timers.h"

namespace PSX
{

Playstation::Playstation() = default;
Playstation::~Playstation() = default;

bool Playstation::Initialize( SDL_Window* window, const fs::path& biosFilename )
{
	m_renderer = std::make_unique<Renderer>();
	if ( !m_renderer->Initialize( window ) )
	{
		LogError( "Failed to initialize renderer" );
		return false;
	}

	m_audioQueue = std::make_unique<AudioQueue>();
	if ( !m_audioQueue->Initialize() )
	{
		LogError( "Failed to initialize audio queue" );
		return false;
	}

	m_bios = std::make_unique<Bios>();
	if ( !LoadBios( biosFilename, *m_bios ) )
	{
		LogError( "Failed to load BIOS [%s]", biosFilename.c_str() );
		return false;
	}

	m_ram = std::make_unique<Ram>();
	m_scratchpad = std::make_unique<Scratchpad>();
	m_memoryControl = std::make_unique<MemoryControl>();
	m_interruptControl = std::make_unique<InterruptControl>();
	m_eventManager = std::make_unique<EventManager>();
	m_mdec = std::make_unique<MacroblockDecoder>( *m_eventManager );

	m_timers = std::make_unique<Timers>( *m_interruptControl, *m_eventManager );

	m_gpu = std::make_unique<Gpu>( *m_interruptControl, *m_renderer, *m_eventManager );

	m_cdromDrive = std::make_unique<CDRomDrive>( *m_interruptControl, *m_eventManager );

	m_spu = std::make_unique<Spu>( *m_cdromDrive, *m_interruptControl, *m_eventManager, *m_audioQueue );

	m_dma = std::make_unique<Dma>( *m_ram, *m_gpu, *m_cdromDrive, *m_mdec, *m_spu, *m_interruptControl, *m_eventManager );

	m_controllerPorts = std::make_unique<ControllerPorts>( *m_interruptControl, *m_eventManager );

	m_memoryMap = std::make_unique<MemoryMap>( *m_bios, *m_cdromDrive, *m_controllerPorts, *m_dma, *m_gpu, *m_interruptControl, *m_mdec, *m_memoryControl, *m_ram, *m_scratchpad, *m_spu, *m_timers );

	m_cpu = std::make_unique<MipsR3000Cpu>( *m_memoryMap, *m_interruptControl, *m_eventManager );

	// resolve circular dependancy
	m_timers->SetGpu( *m_gpu );
	m_gpu->SetTimers( *m_timers );
	m_gpu->SetDma( *m_dma );
	m_mdec->SetDma( *m_dma );
	m_spu->SetDma( *m_dma );
	m_cdromDrive->SetDma( *m_dma );

	return true;
}

void Playstation::Reset()
{
	// reset cycles before events are scheduled
	m_eventManager->Reset();

	m_cdromDrive->Reset();
	m_controllerPorts->Reset();
	m_dma->Reset();
	m_interruptControl->Reset();
	m_mdec->Reset();
	m_memoryControl->Reset();
	m_memoryMap->Reset();
	m_cpu->Reset();
	m_ram->Fill( 0 );
	m_renderer->Reset();
	m_scratchpad->Fill( 0 );
	m_spu->Reset();
	m_timers->Reset();

	// must go after timers reset so it can schedule event
	m_gpu->Reset();

	if ( m_dualSerialPort )
		m_dualSerialPort->Reset();

	m_audioQueue->Clear();
	m_audioQueue->SetPaused( false );
	m_audioQueue->PushSilenceFrames( m_audioQueue->GetDeviceBufferSize() / 2 );
}

void Playstation::SetController( size_t slot, Controller* controller )
{
	m_controllerPorts->SetController( slot, controller );
}

void Playstation::SetMemoryCard( size_t slot, MemoryCard* memCard )
{
	m_controllerPorts->SetMemoryCard( slot, memCard );
}

void Playstation::RunFrame()
{
	m_renderer->StartFrame();

	while ( !m_gpu->GetDisplayFrame() )
		m_cpu->RunUntilEvent();

	m_eventManager->EndFrame();
	m_spu->EndFrame();
	m_gpu->ResetDisplayFrame();
	m_renderer->DisplayFrame();
}

bool Playstation::LoadRom( const fs::path& filename )
{
	auto cdrom = CDRom::Open( filename );
	if ( cdrom )
	{
		Log( "Playstation::LoadRom -- loaded %s", filename.u8string().c_str() );
		m_cdromDrive->SetCDRom( std::move( cdrom ) );
		return true;
	}
	else
	{
		Log( "Playstation::LoadRom -- failed to load %s", filename.u8string().c_str() );
		return false;
	}
}

void Playstation::HookExe( fs::path filename )
{
	m_cpu->SetHookExecutable( std::move( filename ) );
}

float Playstation::GetRefreshRate() const
{
	return m_gpu->GetRefreshRate();
}

void Playstation::DisplayDebugWindow() const
{
	m_gpu->DisplayDebugWindow();
}

}