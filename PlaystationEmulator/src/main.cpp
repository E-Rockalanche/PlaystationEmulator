#include "CPU.h"

#include "MemoryMap.h"

#include <memory>

int main( int, char** )
{
	auto bios = std::make_unique<PSX::Bios>();
	if ( !PSX::LoadBios( "bios.bin", *bios ) )
	{
		std::printf( "Could not load BIOS" );
		return 1;
	}

	auto ram = std::make_unique<PSX::Ram>();
	ram->Fill( char( -1 ) );

	auto scratchpad = std::make_unique<PSX::Scratchpad>();
	scratchpad->Fill( char( -1 ) );

	auto memControl = std::make_unique<PSX::MemoryControl>();

	auto gpu = std::make_unique<PSX::Gpu>();

	auto dmaRegisters = std::make_unique<PSX::Dma>( *ram, *gpu );

	PSX::MemoryMap memoryMap{ *ram, *scratchpad, *memControl, *dmaRegisters, *gpu, *bios };

	auto cpu = std::make_unique<PSX::MipsR3000Cpu>( memoryMap );

	cpu->Reset();

	while ( true )
	{
		cpu->Tick();
	}
}