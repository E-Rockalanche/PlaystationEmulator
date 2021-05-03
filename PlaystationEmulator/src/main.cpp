#include "CPU.h"

#include "MemoryMap.h"

#include <memory>

int main( int argc, char** argv )
{
	auto bios = std::make_unique<PSX::Bios>();
	if ( !PSX::LoadBios( "bios.bin", *bios ) )
	{
		std::printf( "Could not load BIOS" );
		return 1;
	}

	auto ram = std::make_unique<PSX::Ram>();
	ram->Fill( 0u );

	PSX::MemoryMap memoryMap{ *bios, *ram };

	auto cpu = std::make_unique<PSX::MipsR3000Cpu>( memoryMap );

	while ( true )
	{
		cpu->Tick();
	}
}