#include "CDRom.h"

#include <fstream>

namespace PSX
{

class CDRom_Bin : public CDRom
{
public:
	CDRom_Bin() = default;
	~CDRom_Bin() override = default;

	bool Open( const fs::path & filename );

	bool ReadSectorFromIndex( const Index& index, LogicalSector position, Sector& sector ) override;

private:
	static constexpr uint32_t PregapLength = 2 * SectorsPerSecond;
	static constexpr Track::Type TrackType = Track::Type::Mode2_2352;

private:
	std::ifstream m_binFile;
};

bool CDRom_Bin::Open( const fs::path& filename )
{
	m_binFile.open( filename, std::ios::binary );
	if ( !m_binFile.is_open() )
		return false;

	// get file size
	m_binFile.seekg( 0, std::ios::end );
	const auto totalSectors = static_cast<uint32_t>( m_binFile.tellg() / BytesPerSector );
	m_binFile.seekg( 0, std::ios::beg );
	if ( totalSectors == 0 )
		return false;

	// two seconds of implicit pregap
	Index pregapIndex{};
	pregapIndex.indexNumber = 0;
	pregapIndex.trackNumber = 1;
	pregapIndex.position = 0;
	pregapIndex.positionInTrack = static_cast<LogicalSector>( -static_cast<int32_t>( PregapLength ) );
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
	dataIndex.length = totalSectors;
	dataIndex.trackType = TrackType;
	dataIndex.pregap = false;
	m_indices.push_back( dataIndex );

	// single track
	Track track{};
	track.trackNumber = 1;
	track.position = PregapLength;
	track.length = totalSectors;
	track.firstIndex = 0;
	track.type = TrackType;
	m_tracks.push_back( track );

	SeekTrack1();

	return true;
}

bool CDRom_Bin::ReadSectorFromIndex( const Index& index, LogicalSector position, Sector& sector )
{
	const uint32_t diskPosition = index.position + position;
	if ( diskPosition < PregapLength )
		return false;

	const uint32_t physicalPos = diskPosition - PregapLength;
	m_binFile.seekg( physicalPos * BytesPerSector );
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