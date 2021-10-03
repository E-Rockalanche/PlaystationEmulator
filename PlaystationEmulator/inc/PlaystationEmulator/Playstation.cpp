#include "Playstation.h"

#include "BIOS.h"
#include "CDRom.h"
#include "CDRomDrive.h"
#include "Controller.h"
#include "ControllerPorts.h"
#include "CPU.h"
#include "CycleScheduler.h"
#include "DMA.h"
#include "File.h"
#include "GPU.h"
#include "MemoryControl.h"
#include "Renderer.h"
#include "SPU.h"
#include "Timers.h"

namespace PSX
{

Playstation::Playstation() = default;
Playstation::~Playstation() = default;

bool Playstation::Initialize( SDL_Window* window, const char* biosFilename )
{
	m_renderer = std::make_unique<Renderer>();
	if ( !m_renderer->Initialize( window ) )
	{
		LogError( "Failed to initialize renderer" );
		return false;
	}

	m_bios = std::make_unique<Bios>();
	if ( !LoadBios( biosFilename, *m_bios ) )
	{
		LogError( "Failed to load BIOS [%s]", biosFilename );
		return false;
	}

	m_ram = std::make_unique<Ram>();
	m_scratchpad = std::make_unique<Scratchpad>();

	m_memoryControl = std::make_unique<MemoryControl>();
	m_interruptControl = std::make_unique<InterruptControl>();
	m_cycleScheduler = std::make_unique<CycleScheduler>();
	m_spu = std::make_unique<Spu>();

	m_timers = std::make_unique<Timers>( *m_interruptControl, *m_cycleScheduler );

	m_gpu = std::make_unique<Gpu>( *m_timers, *m_interruptControl, *m_renderer, *m_cycleScheduler );

	m_cdromDrive = std::make_unique<CDRomDrive>( *m_interruptControl, *m_cycleScheduler );

	m_dma = std::make_unique<Dma>( *m_ram, *m_gpu, *m_cdromDrive, *m_interruptControl, *m_cycleScheduler );

	m_controllerPorts = std::make_unique<ControllerPorts>( *m_interruptControl, *m_cycleScheduler );

	m_memoryMap = std::make_unique<MemoryMap>( *m_ram, *m_scratchpad, *m_memoryControl, *m_controllerPorts, *m_interruptControl, *m_dma, *m_timers, *m_cdromDrive, *m_gpu, *m_spu, *m_bios );

	m_cpu = std::make_unique<MipsR3000Cpu>( *m_memoryMap, *m_ram, *m_bios, *m_scratchpad, *m_interruptControl, *m_cycleScheduler );

	Reset();

	return true;
}

void Playstation::Reset()
{
	m_cdromDrive->Reset();
	m_controllerPorts->Reset();
	m_cpu->Reset();
	m_cycleScheduler->Reset();
	m_dma->Reset();
	m_gpu->Reset();
	m_interruptControl->Reset();
	m_memoryControl->Reset();
	m_renderer->Reset();
	m_spu->Reset();
	m_timers->Reset();

	m_cycleScheduler->ScheduleNextUpdate();
}

void Playstation::SetController( size_t slot, Controller* controller )
{
	m_controllerPorts->SetController( slot, controller );
}

void Playstation::RunFrame()
{
	static constexpr uint32_t HookAddress = 0x80030000;

	while ( !m_gpu->GetDisplayFrame() )
	{
		if ( m_exeFilename && m_cpu->GetPC() == HookAddress )
		{
			LoadExecutable( m_exeFilename, *m_cpu, *m_ram );
			m_exeFilename = nullptr;
		}

		m_cpu->Tick();
	}

	m_gpu->ResetDisplayFrame();
	m_renderer->DisplayFrame();
}

void Playstation::LoadRom( const char* filename )
{
	auto cdrom = std::make_unique<PSX::CDRom>();
	if ( cdrom->Open( filename ) )
		m_cdromDrive->SetCDRom( std::move( cdrom ) );
}

void Playstation::HookExe( const char* filename )
{
	m_exeFilename = filename;
}

float Playstation::GetRefreshRate() const
{
	return m_gpu->GetRefreshRate();
}

}