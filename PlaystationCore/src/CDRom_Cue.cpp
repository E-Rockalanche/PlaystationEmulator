#include "CDRom.h"

#include "CueSheet.h"

#include <fstream>

namespace PSX
{

class CDRom_Cue : public CDRom
{
public:
	bool Open( const fs::path& filename );

	bool ReadSectorFromIndex( const Index& index, LogicalSector position, Sector& sector ) const override;

private:

	static constexpr size_t InvalidFileIndex = std::numeric_limits<size_t>::max();

	size_t FindFileEntryIndex( std::string_view filename ) const
	{
		for ( size_t i = 0; i < m_binFiles.size(); ++i )
			if ( m_binFiles[ i ].filename == filename )
				return i;

		return InvalidFileIndex;
	}

private:
	struct FileEntry
	{
		std::string filename;
		mutable std::ifstream binFile;
		uint32_t sectorCount = 0;
	};

	std::vector<FileEntry> m_binFiles;
};

bool CDRom_Cue::Open( const fs::path& filename )
{
	CueSheet cueSheet;
	if ( !CueSheet::Load( filename, cueSheet ) )
	{
		LogError( "Failed to load cue sheet %s", filename.u8string().c_str() );
		return false;
	}

	const fs::path parentDir = filename.parent_path();

	LogicalSector currentDiskPosition = 0;

	for ( uint32_t trackNumber = 1; trackNumber <= CueSheet::MaxTracks; ++trackNumber )
	{
		const auto [cueTrack, cueFile] = cueSheet.FindTrack( trackNumber );
		if ( cueTrack == nullptr )
			break;

		uint32_t fileIndex = FindFileEntryIndex( cueFile->filename );
		uint32_t fileSectorCount = 0;

		const fs::path binFilename = parentDir / cueFile->filename;
		if ( fileIndex == InvalidFileIndex )
		{
			// open bin file

			std::ifstream fin( binFilename, std::ios::binary );
			if ( !fin.is_open() )
			{
				LogError( "Could not open file %s", binFilename.u8string().c_str() );
				return false;
			}

			fin.seekg( 0, std::ios::end );
			fileSectorCount = static_cast<uint32_t>( fin.tellg() / BytesPerSector );
			fin.seekg( 0, std::ios::beg );
			if ( fileSectorCount == 0 )
			{
				LogError( "File %s is too small", binFilename.u8string().c_str() );
				return false;
			}

			fileIndex = static_cast<uint32_t>( m_binFiles.size() );
			m_binFiles.push_back( FileEntry{ cueFile->filename, std::move( fin ), fileSectorCount } );
		}
		else
		{
			fileSectorCount = m_binFiles[ fileIndex ].sectorCount;
		}

		Track::Type trackType{};
		switch ( cueTrack->type )
		{
			case CueSheet::Track::Type::Audio:		trackType = Track::Type::Audio;			break;
			case CueSheet::Track::Type::Mode1_2048:	trackType = Track::Type::Mode1_2048;	break;
			case CueSheet::Track::Type::Mode1_2352:	trackType = Track::Type::Mode1_2352;	break;
			case CueSheet::Track::Type::Mode2_2336:	trackType = Track::Type::Mode2_2336;	break;
			case CueSheet::Track::Type::Mode2_2352:	trackType = Track::Type::Mode2_2352;	break;

			default:
				LogError( "Track type is unsupported" );
				return false;
		}

		auto* index1 = cueTrack->FindIndex( 1 );
		dbAssert( index1 );
		uint32_t trackFileStart = index1->location.ToLogicalSector();

		// get track length
		uint32_t trackLength = 0;
		auto[ nextTrack, nextFile ] = cueSheet.FindTrack( trackNumber + 1 );
		if ( nextTrack && nextFile->filename == cueFile->filename )
		{
			auto* nextIndex = nextTrack->FindIndex( 0 );

			if ( !nextIndex )
				nextIndex = nextTrack->FindIndex( 1 );

			trackLength = nextIndex->location.ToLogicalSector() - trackFileStart;
		}
		else
		{
			if ( trackFileStart >= fileSectorCount )
			{
				LogError( "Track %u file position [%u] exceeds file %s length [%u]", trackNumber, trackFileStart, binFilename.u8string().c_str(), fileSectorCount );
				return false;
			}

			trackLength = fileSectorCount - trackFileStart;
		}

		auto* pregapIndex = cueTrack->FindIndex( 0 );
		if ( pregapIndex )
		{
			// explicit pregap

			const uint32_t pregapLength = trackFileStart - pregapIndex->location.ToLogicalSector();

			Index pregap{};
			pregap.indexNumber = 0;
			pregap.trackNumber = trackNumber;
			pregap.position = currentDiskPosition;
			pregap.positionInTrack = (uint32_t)-(int32_t)pregapLength;
			pregap.length = pregapLength;
			pregap.trackType = trackType;
			pregap.pregap = true;

			pregap.fileIndex = fileIndex;
			pregap.filePosition = trackFileStart - pregapLength;

			m_indices.push_back( pregap );
			currentDiskPosition += pregapLength;
		}
		else
		{
			const bool isMultiTrackBin = ( trackNumber > 1 && fileIndex == m_indices.back().fileIndex );
			const bool likelyCDAudio = ( cueSheet.FindTrack( 1 ).first->type == CueSheet::Track::Type::Audio );

			uint32_t pregapLength = 0;
			if ( cueTrack->pregap.has_value() )
				pregapLength = cueTrack->pregap->ToLogicalSector();
			else if ( trackNumber == 1 || isMultiTrackBin || !likelyCDAudio )
				pregapLength = PregapLength;

			if ( pregapLength > 0 )
			{
				Index pregap{};
				pregap.indexNumber = 0;
				pregap.trackNumber = 1;
				pregap.position = currentDiskPosition;
				pregap.positionInTrack = (uint32_t)-(int32_t)pregapLength;
				pregap.length = pregapLength;
				pregap.trackType = trackType;
				pregap.pregap = true;

				m_indices.push_back( pregap );
				currentDiskPosition += pregapLength;
			}
		}
		
		// add the track
		Track track{};
		track.trackNumber = trackNumber;
		track.position = currentDiskPosition;
		track.length = trackLength;
		track.firstIndex = static_cast<uint32_t>( m_indices.size() );
		track.type = trackType;
		m_tracks.push_back( track );

		// add non-pregap indices
		uint32_t currentTrackPosition = 0;
		for ( uint32_t indexNumber = 1; indexNumber <= CueSheet::MaxIndices; ++indexNumber )
		{
			auto* cueIndex = cueTrack->FindIndex( indexNumber );
			if ( !cueIndex )
				break;

			auto* nextIndex = cueTrack->FindIndex( indexNumber + 1 );
			const uint32_t indexLength = nextIndex
				? nextIndex->location.ToLogicalSector() - cueIndex->location.ToLogicalSector()
				: trackLength;

			if ( currentTrackPosition >= trackLength )
			{
				LogError( "Index %u track position [%u] exceeds track %u length [%u]", indexNumber, currentTrackPosition, trackNumber, trackLength );
				return false;
			}

			Index index{};
			index.indexNumber = indexNumber;
			index.trackNumber = trackNumber;
			index.position = currentDiskPosition;
			index.positionInTrack = currentTrackPosition;
			index.length = indexLength;
			index.trackType = trackType;
			index.pregap = false;
			index.fileIndex = fileIndex;
			index.filePosition = trackFileStart + currentTrackPosition;
			m_indices.push_back( index );

			currentTrackPosition += indexLength;
			currentDiskPosition += indexLength;
		}
	}

	if ( m_tracks.empty() )
	{
		LogError( "File %s has no tracks", filename.u8string().c_str() );
		return false;
	}

	m_filename = filename;

	AddLeadOutIndex();

	return SeekTrack1();
}

bool CDRom_Cue::ReadSectorFromIndex( const Index& index, LogicalSector position, Sector& sector ) const
{
	const auto filePos = ( std::streampos( index.filePosition ) + std::streampos( position ) ) * BytesPerSector;

	auto& binFile = m_binFiles[ index.fileIndex ].binFile;
	binFile.seekg( filePos );
	if ( binFile.fail() )
		return false;

	binFile.read( (char*)&sector, sizeof( Sector ) );
	return binFile.good();
}

std::unique_ptr<CDRom> CDRom::OpenCue( const fs::path& filename )
{
	auto cdrom = std::make_unique<CDRom_Cue>();
	if ( cdrom->Open( filename ) )
		return cdrom;

	return nullptr;
}

}