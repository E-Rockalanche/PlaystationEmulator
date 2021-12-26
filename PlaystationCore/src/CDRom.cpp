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
			return false;
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
		return false;

	auto& track = m_tracks[ trackNumber - 1 ];
	const uint32_t positionInTrack = locationInTrack.ToLogicalSector();
	if ( positionInTrack >= track.length )
		return false;

	return Seek( track.position + positionInTrack );
}

const CDRom::Index* CDRom::FindIndex( LogicalSector position ) const
{
	for ( auto& index : m_indices )
	{
		if ( Within( position, index.position, index.length ) )
			return &index;
	}
	return nullptr;
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
	if ( stdx::iequals( ext.native(), ".bin" ) )
	{
		return OpenBin( filename );
	}
	else
	{
		return nullptr;
	}
}

}