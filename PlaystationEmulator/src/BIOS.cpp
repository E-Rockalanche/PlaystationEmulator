#include "BIOS.h"

#include <fstream>

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

	fin.read( bios.Data(), BiosSize );
	fin.close();

	return true;
}


}