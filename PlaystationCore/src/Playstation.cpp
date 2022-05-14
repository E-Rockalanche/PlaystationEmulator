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
#include "SaveState.h"
#include "SerialPort.h"
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

	m_serialPort = std::make_unique<SerialPort>();

	m_memoryMap = std::make_unique<MemoryMap>( *m_bios, *m_cdromDrive, *m_controllerPorts, *m_dma, *m_gpu, *m_interruptControl, *m_mdec, *m_memoryControl, *m_ram, *m_scratchpad, *m_serialPort, *m_spu, *m_timers );

	m_cpu = std::make_unique<MipsR3000Cpu>( *m_memoryMap, *m_interruptControl, *m_eventManager );

	// resolve circular dependancies
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
	m_serialPort->Reset();
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
	while ( !m_gpu->GetDisplayFrame() )
		m_cpu->RunUntilEvent();

	m_eventManager->EndFrame();
	m_spu->EndFrame();
	m_gpu->ResetDisplayFrame();
	m_renderer->DisplayFrame();
}

void Playstation::SetCDRom( std::unique_ptr<CDRom> cdrom )
{
	m_cdromDrive->SetCDRom( std::move( cdrom ) );
}

CDRom* Playstation::GetCDRom()
{
	return m_cdromDrive->GetCDRom();
}

void Playstation::HookExe( fs::path filename )
{
	m_cpu->SetHookExecutable( std::move( filename ) );
}

float Playstation::GetRefreshRate() const
{
	return m_gpu->GetRefreshRate();
}

bool Playstation::Serialize( SaveStateSerializer& serializer )
{
	if ( !serializer.Header( "PSX", 1 ) )
		return false;

	serializer( m_bios->Data(), m_bios->Size() );
	serializer( m_ram->Data(), m_ram->Size() );
	serializer( m_scratchpad->Data(), m_scratchpad->Size() );

	m_cdromDrive->Serialize( serializer );
	m_controllerPorts->Serialize( serializer );
	m_dma->Serialize( serializer );
	m_gpu->Serialize( serializer );
	m_interruptControl->Serialize( serializer );
	m_mdec->Serialize( serializer );
	m_memoryControl->Serialize( serializer );
	m_memoryMap->Serialize( serializer );
	m_cpu->Serialize( serializer );
	m_spu->Serialize( serializer );
	m_timers->Serialize( serializer );

	// must be deserialized last so it can schedule next event
	m_eventManager->Serialize( serializer );

	return serializer.End();
}

}