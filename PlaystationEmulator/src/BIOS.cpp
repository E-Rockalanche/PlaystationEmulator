#include "BIOS.h"

#include <fstream>

#include "Instruction.h"

namespace PSX
{

bool LoadBios( const char* filename, Bios& bios )
{
	std::ifstream fin( filename, std::ios::binary );
	if ( !fin.is_open() )
		return false;

	fin.seekg( 0, std::ios::end );
	const auto biosSize = fin.tellg();
	if ( biosSize != BiosSize )
		return false;

	fin.seekg( 0, std::ios::beg );

	fin.read( (char*)bios.Data(), BiosSize );
	fin.close();

	// patch BIOS to force TTY output (seems to require proper dual serial port)
	// bios.Write<uint32_t>( 0x1bc3 << 2, 0x24010001 );
	// bios.Write<uint32_t>( 0x1bc5 << 2, 0xaf81a9c0 );

	return true;
}


}