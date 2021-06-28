#include "MemoryMap.h"

namespace PSX
{

std::pair<MemoryMap::Segment, uint32_t> MemoryMap::TranslateAddress( uint32_t address ) const noexcept
{
	// upper 3 bits determine region
	// convert virtual address to physical address
	address &= RegionMasks[ address >> 29 ];

	for ( uint32_t i = 0; i < Segments.size(); ++i )
	{
		auto& region = Segments[ i ];
		if ( region.Contains( address ) )
			return { static_cast<Segment>( i ), address - region.start };
	}

	return { Segment::Invalid, 0 };
}

}