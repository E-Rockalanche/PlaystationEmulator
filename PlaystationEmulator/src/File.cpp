#include "File.h"

#include "CPU.h"
#include "RAM.h"

#include <fstream>

namespace PSX
{

bool LoadExecutable( const fs::path& filename, PSX::MipsR3000Cpu& cpu, PSX::Ram& ram )
{
	std::ifstream fin( filename, std::ios::binary );
	if ( !fin.is_open() )
	{
		dbBreakMessage( "failed to open executable file" );
		dbBreak();
		return false;
	}

	fin.seekg( 0, std::ios_base::end );
	const size_t fileSize = static_cast<size_t>( fin.tellg() );
	fin.seekg( 0, std::ios_base::beg );

	if ( ( fileSize / 0x800 < 2 ) || ( fileSize % 0x800 != 0 ) )
	{
		dbLogError( "file size must be a multiple of 0x800 greater than 1 [%x]", static_cast<uint32_t>( fileSize ) );
		dbBreak();
		return false;
	}

	PSX::ExeHeader header;
	fin.read( reinterpret_cast<char*>( &header ), sizeof( header ) );

	if ( header.id != PSX::ExeHeader::Id )
	{
		header.id[ 0xf ] = '\0';
		dbLogError( "header ID is invalid [%s]", header.id );
		dbBreak();
		return false;
	}

	if ( header.fileSize > fileSize - sizeof( header ) )
	{
		dbLogError( "header file size is greater than actual file size [%x] [%x]", header.fileSize, static_cast<uint32_t>( fileSize ) );
		dbBreak();
		return false;
	}

	const auto physicalRamDest = header.ramDestination & 0x7fffffff;

	if ( physicalRamDest + header.fileSize > PSX::Ram::Size() )
	{
		dbLogError( "file size larger than ram at destination [%x] [%x]", header.fileSize, header.ramDestination );
		dbBreak();
		return false;
	}

	// TODO: zero fill

	fin.read( (char*)ram.Data() + physicalRamDest, header.fileSize );

	cpu.DebugSetProgramCounter( header.programCounter );

	cpu.DebugSetRegister( 28, header.globalPointer );

	if ( header.stackPointerBase != 0 )
	{
		cpu.DebugSetRegister( 29, header.stackPointerBase + header.stackPointerOffset ); // TODO: is this right?
		cpu.DebugSetRegister( 30, header.stackPointerBase ); // TODO: is this right?
	}

	dbLog( "loaded %s", filename );

	return true;
}

}