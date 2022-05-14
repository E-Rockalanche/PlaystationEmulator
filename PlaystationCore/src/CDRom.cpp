#include "CDRom.h"

#include "SaveState.h"

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

bool CDRom::Seek( LogicalSector position ) noexcept
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

bool CDRom::Seek( uint32_t trackNumber, Location locationInTrack ) noexcept
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

const CDRom::Index* CDRom::FindIndex( LogicalSector position ) const noexcept
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

bool CDRom::ReadSector( Sector& sector, SubQ& subq )
{
	dbAssert( m_currentIndex );

	if ( m_positionInIndex == m_currentIndex->length )
	{
		// get the next index info
		if ( !Seek( m_position ) )
			return false;
	}

	if ( !ReadSector( sector ) )
		return false;

	subq = GetSubQFromIndex( *m_currentIndex, m_position );

	++m_position;
	++m_positionInIndex;
	++m_positionInTrack;

	return true;
}

bool CDRom::ReadSubQ( SubQ& subq ) const
{
	if ( !m_currentIndex )
		return false;

	subq = GetSubQFromIndex( *m_currentIndex, m_position );
	return true;
}

bool CDRom::ReadSector( Sector& sector ) const
{
	if ( !m_currentIndex )
		return false;

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

	return true;
}

bool CDRom::ReadSubQFromPosition( LogicalSector position, SubQ& subq ) const
{
	const Index* index = FindIndex( position );
	if ( !index )
		return false;
	
	subq = GetSubQFromIndex( *index, position );
	return true;
}

CDRom::SubQ CDRom::GetSubQFromIndex( const Index& index, LogicalSector position ) noexcept
{
	const uint8_t trackNumberBCD = ( index.trackNumber == CDRom::LeadOutTrackNumber ) ? CDRom::LeadOutTrackNumber : BinaryToBCD( static_cast<uint8_t>( index.trackNumber ) );
	const Location trackLocation = Location::FromLogicalSector( position - index.position + index.positionInTrack );
	const Location diskLocation = Location::FromLogicalSector( position );

	SubQ subq;
	subq.control.adr = 1;
	subq.control.dataSector = index.trackType != Track::Type::Audio;
	subq.trackNumberBCD = trackNumberBCD;
	subq.trackIndexBCD = BinaryToBCD( static_cast<uint8_t>( index.indexNumber ) );
	subq.trackMinuteBCD = BinaryToBCD( trackLocation.minute );
	subq.trackSecondBCD = BinaryToBCD( trackLocation.second );
	subq.trackSectorBCD = BinaryToBCD( trackLocation.sector );
	subq.absoluteMinuteBCD = BinaryToBCD( diskLocation.minute );
	subq.absoluteSecondBCD = BinaryToBCD( diskLocation.second );
	subq.absoluteSectorBCD = BinaryToBCD( diskLocation.sector );
	return subq;
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