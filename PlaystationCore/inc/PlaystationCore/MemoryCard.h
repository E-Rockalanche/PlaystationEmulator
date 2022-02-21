#pragma once

#include <stdx/compiler.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

namespace PSX
{

class SaveStateSerializer;

class MemoryCard
{
public:
	// load existing memory card
	static std::unique_ptr<MemoryCard> Load( fs::path filename );

	// create freshly formatted memory card
	static std::unique_ptr<MemoryCard> Create( fs::path filename );

	void Reset();
	void ResetTransfer();

	bool Communicate( uint8_t dataIn, uint8_t& dataOut );

	void Format();

	bool Save();

	bool SaveAs( fs::path filename )
	{
		m_filename = std::move( filename );
		return Save();
	}

	const fs::path& GetFilename() const noexcept
	{
		return m_filename;
	}

	bool Written() const noexcept
	{
		return m_written;
	}

	void Serialize( SaveStateSerializer& serializer );

private:
	static constexpr uint32_t TotalSize = 128 * 1024;
	static constexpr uint32_t SectorCount = 1024;
	static constexpr uint32_t SectorSize = 128;
	static constexpr uint32_t BlockCount = 16;
	static constexpr uint32_t BlockSize = 8 * 1024;

	enum class State
	{
		Idle,
		Command, // <R>ead, <W>rite, <S> ID

		Read_ID1,
		Read_ID2,
		Read_AddressHigh,
		Read_AddressLow,
		Read_CommandAck1,
		Read_CommandAck2,
		Read_ConfirmAddressHigh,
		Read_ConfirmAddressLow,
		Read_Data,
		Read_Checksum,
		Read_EndByte,

		Write_ID1,
		Write_ID2,
		Write_AddressHigh,
		Write_AddressLow,
		Write_Data,
		Write_Checksum,
		Write_CommandAck1,
		Write_CommandAck2,
		Write_EndByte,

		ID_ID1,
		ID_ID2,
		ID_CommandAck1,
		ID_CommandAck2,
		ID_SectorCountHigh,
		ID_SectorCountLow,
		ID_SectorSizeHigh,
		ID_SectorSizeLow
	};

	union Flag
	{
		struct
		{
			uint8_t : 2;
			uint8_t writeError : 1;
			uint8_t directoryNotRead : 1;
			uint8_t : 4;
		};
		uint8_t value = 0x08; // directory not read
	};
	static_assert( sizeof( Flag ) == 1 );

	struct HeaderFrame
	{
		std::array<char, 2> id{ 'M', 'C' };
		std::array<uint8_t, SectorSize - 3> zero{};
		uint8_t checksum = 0x0e;
	};
	static_assert( sizeof( HeaderFrame ) == SectorSize );

	enum class BlockAllocationState : uint32_t
	{
		Used_First = 0x51,
		Used_Middle = 0x52,
		Used_Last = 0x53,
		Free_Fresh = 0xa0,
		Free_DeletedFirst = 0xa1,
		Free_DeletedMiddle = 0xa2,
		Free_DeletedLast = 0xa3
	};

	struct DirectoryFrame
	{
		BlockAllocationState blockAllocationState = BlockAllocationState::Free_Fresh;
		uint32_t fileSize = 0;
		uint16_t nextBlock = 0xffffu;
		std::array<char, 20> filename{};
		std::array<uint8_t, 0x5f> garbage{};
		uint8_t checksum = 0xa0;
	};
	static_assert( sizeof( DirectoryFrame ) == SectorSize );

	struct BrokenSectorList
	{
		uint32_t brokenSectorNumber = 0xffffffffu;
		std::array<uint8_t, SectorSize - 5> garbage{};
		uint8_t checksum = 0;
	};
	static_assert( sizeof( BrokenSectorList ) == SectorSize );

	static constexpr uint8_t HighZ = 0xff;

	static constexpr uint8_t Good = 0x47;
	static constexpr uint8_t BadChecksum = 0x4e;
	static constexpr uint8_t BadSector = 0xff;

private:
	MemoryCard() = default;

	uint8_t GetCurrentSectorChecksum() const;

private:

	State m_state = State::Idle;
	Flag m_flag;

	uint32_t m_dataCount = 0;
	uint16_t m_address = 0;
	uint8_t m_previousData = 0;
	uint8_t m_writeChecksum = 0;

	// serialize this?
	fs::path m_filename;
	std::array<uint8_t, TotalSize> m_memory{};
	bool m_written = false;
};

}