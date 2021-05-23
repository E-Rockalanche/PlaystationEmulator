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

	PSX::MemoryMap memoryMap{ *ram, *scratchpad, *memControl, *bios };

	auto cpu = std::make_unique<PSX::MipsR3000Cpu>( memoryMap );

	while ( true )
	{
		cpu->Tick();
	}
}