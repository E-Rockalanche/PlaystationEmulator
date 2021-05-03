#include "MemoryMap.h"

namespace PSX
{

std::pair<MemoryMap::SegmentType, uint32_t> MemoryMap::TranslateAddress( uint32_t address ) const noexcept
{
	// upper 3 bits determine region
	// convert virtual address to physical address
	address &= RegionMasks[ address >> 29 ];

	for ( uint32_t i = 0; i < Regions.size(); ++i )
	{
		auto& region = Regions[ i ];
		if ( region.Contains( address ) )
			return { static_cast<SegmentType>( i ), address - region.start };
	}

	// operates on virtual address since KSEG2 doesn't mirror other regions
	if ( IoStart <= address && address < IoStart + IoSize )
		return { SegmentType::IoPorts, address - IoStart };

	return { SegmentType::Invalid, 0 };
}

}