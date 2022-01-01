#include "CDRom.h"

#include <fstream>

namespace PSX
{

class CDRom_Bin : public CDRom
{
public:
	bool Open( const fs::path & filename );

	bool ReadSectorFromIndex( const Index& index, LogicalSector position, Sector& sector ) override;

private:
	static constexpr Track::Type TrackType = Track::Type::Mode2_2352;

private:
	std::ifstream m_binFile;
};

bool CDRom_Bin::Open( const fs::path& filename )
{
	std::ifstream fin( filename, std::ios::binary );
	if ( !fin.is_open() )
		return false;

	// get file size
	fin.seekg( 0, std::ios::end );
	const auto fileSectorCount = static_cast<uint32_t>( fin.tellg() / BytesPerSector );
	fin.seekg( 0, std::ios::beg );
	if ( fileSectorCount == 0 )
		return false;

	m_binFile = std::move( fin );
	m_filename = filename;

	// build TOC

	// two seconds of implicit pregap
	Index pregapIndex{};
	pregapIndex.indexNumber = 0;
	pregapIndex.trackNumber = 1;
	pregapIndex.position = 0;
	pregapIndex.positionInTrack = (uint32_t)-(int32_t)PregapLength;
	pregapIndex.length = PregapLength;
	pregapIndex.trackType = TrackType;
	pregapIndex.pregap = true;
	m_indices.push_back( pregapIndex );

	// data index
	Index dataIndex{};
	dataIndex.indexNumber = 1;
	dataIndex.trackNumber = 1;
	dataIndex.position = PregapLength;
	dataIndex.positionInTrack = 0;
	dataIndex.length = fileSectorCount;
	dataIndex.trackType = TrackType;
	dataIndex.pregap = false;
	dataIndex.filePosition = 0;
	m_indices.push_back( dataIndex );

	// single track
	Track track{};
	track.trackNumber = 1;
	track.position = PregapLength;
	track.length = fileSectorCount;
	track.firstIndex = 0;
	track.type = TrackType;
	m_tracks.push_back( track );

	AddLeadOutIndex();

	return SeekTrack1();
}

bool CDRom_Bin::ReadSectorFromIndex( const Index& index, LogicalSector position, Sector& sector )
{
	const auto filePos = ( std::streampos( index.filePosition ) + std::streampos( position ) ) * BytesPerSector;

	m_binFile.seekg( filePos );
	if ( m_binFile.fail() )
		return false;

	m_binFile.read( (char*)&sector, sizeof( Sector ) );
	return m_binFile.good();
}

std::unique_ptr<CDRom> CDRom::OpenBin( const fs::path& filename )
{
	auto cdrom = std::make_unique<CDRom_Bin>();
	if ( cdrom->Open( filename ) )
		return cdrom;

	return nullptr;
}

}