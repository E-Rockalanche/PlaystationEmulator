#include "CDRom.h"

#include <stdx/string.h>

namespace PSX
{

namespace
{

constexpr bool Within( uint32_t value, uint32_t base, uint32_t length ) noexcept
{
	return ( base <= value ) && ( value < base + length );
}

}

bool CDRom::Seek( LogicalSector position )
{
	const Index* newIndex = nullptr;

	if ( m_currentIndex && Within( position, m_currentIndex->position, m_currentIndex->length ) )
	{
		newIndex = m_currentIndex;
	}
	else
	{
		newIndex = FindIndex( position );
		if ( !newIndex )
		{
			dbLogWarning( "CDRom::Seek -- failed seek to sector %u", position );
			return false;
		}
	}

	m_position = position;
	m_currentIndex = newIndex;
	m_positionInIndex = position - newIndex->position;
	m_positionInTrack = newIndex->positionInTrack + m_positionInIndex;
	return true;
}

bool CDRom::Seek( uint32_t trackNumber, Location locationInTrack )
{
	if ( trackNumber == 0 || trackNumber > m_tracks.size() )
	{
		dbLogWarning( "CDRom::Seek -- track number out of bounds [%u]", trackNumber );
		return false;
	}

	auto& track = m_tracks[ trackNumber - 1 ];
	const uint32_t positionInTrack = locationInTrack.ToLogicalSector();
	if ( positionInTrack >= track.length )
	{
		dbLogWarning( "CDRom::Seek -- failed seek to track %u, location %u:%u:%u", trackNumber, locationInTrack.minute, locationInTrack.second, locationInTrack.sector );
		return false;
	}

	return Seek( track.position + positionInTrack );
}

const CDRom::Index* CDRom::FindIndex( LogicalSector position ) const
{
	for ( auto& index : m_indices )
	{
		if ( Within( position, index.position, index.length ) )
			return &index;
	}

	dbLogWarning( "CDRom::FindIndex -- cannot find index for disk position %u", position );
	return nullptr;
}

void CDRom::AddLeadOutIndex()
{
	dbAssert( !m_indices.empty() );
	auto& lastIndex = m_indices.back();

	Index leadOut{};
	leadOut.indexNumber = 0;
	leadOut.trackNumber = LeadOutTrackNumber;
	leadOut.position = lastIndex.position + lastIndex.length;
	leadOut.length = LeadOutLength;
	m_indices.push_back( leadOut );
}

bool CDRom::ReadSector( Sector& sector )
{
	dbAssert( m_currentIndex );

	if ( m_positionInIndex == m_currentIndex->length )
	{
		// get the next index info
		if ( !Seek( m_position ) )
			return false;
	}

	if ( m_currentIndex->trackNumber == LeadOutTrackNumber )
	{
		sector.rawData.fill( LeadOutTrackNumber );
	}
	else if ( m_currentIndex->pregap )
	{
		sector.rawData.fill( 0 );
	}
	else if ( !ReadSectorFromIndex( *m_currentIndex, m_positionInIndex, sector ) )
	{
		return false;
	}

	// TODO: read sub channel Q

	++m_position;
	++m_positionInIndex;
	++m_positionInTrack;
	return true;
}

std::unique_ptr<CDRom> CDRom::Open( const fs::path& filename )
{
	const auto ext = filename.extension();

	std::unique_ptr<CDRom> cdrom;

	if ( stdx::iequals( ext.native(), ".bin" ) )
	{
		cdrom =  OpenBin( filename );
	}
	else if ( stdx::iequals( ext.native(), ".cue" ) )
	{
		cdrom =  OpenCue( filename );
	}

	if ( cdrom == nullptr )
	{
		LogError( "Failed to open %s", filename.u8string().c_str() );
	}

	return cdrom;
}

}