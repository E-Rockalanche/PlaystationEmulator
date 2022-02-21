#include "MemoryCard.h"

#include "SaveState.h"

#include <stdx/assert.h>

#include <fstream>

namespace PSX
{

namespace
{

template <typename T>
constexpr T IncrementEnum( T value ) noexcept
{
	return static_cast<T>( static_cast<int>( value ) + 1 );
}

constexpr uint8_t XorChecksum( const uint8_t* first, size_t count ) noexcept
{
	uint8_t checksum = 0;
	for ( ; count > 0; --count, ++first )
		checksum ^= *first;

	return checksum;
}

}

std::unique_ptr<MemoryCard> MemoryCard::Load( fs::path filename )
{
	std::ifstream fin( filename, std::ios::binary );
	if ( !fin.is_open() )
	{
		dbLogWarning( "MemoryCard::Load -- Cannot open file [%s]", filename.c_str() );
		return nullptr;
	}

	// get file size
	fin.seekg( 0, std::ios::end );
	const auto size = fin.tellg();
	if ( size != TotalSize )
	{
		dbLogWarning( "MemoryCard::Load -- file size is invalid [%X]", static_cast<uint64_t>( size ) );
		return nullptr;
	}
	fin.seekg( 0, std::ios::beg );

	std::unique_ptr<MemoryCard> card( new MemoryCard() ); // private constructor

	fin.read( (char*)card->m_memory.data(), TotalSize );
	fin.close();

	card->m_filename = std::move( filename );

	return card;
}

std::unique_ptr<MemoryCard> MemoryCard::Create( fs::path filename )
{
	std::unique_ptr<MemoryCard> card( new MemoryCard() ); // private constructor
	card->Format();
	card->m_filename = std::move( filename );
	return card;
}

void MemoryCard::Reset()
{
	ResetTransfer();
	m_flag = Flag{};
}

void MemoryCard::ResetTransfer()
{
	m_state = State::Idle;
	m_dataCount = 0;
	m_address = 0;
	m_previousData = 0;
	m_writeChecksum = 0;
}

bool MemoryCard::Communicate( uint8_t input, uint8_t& output )
{
	switch ( m_state )
	{
		case State::Idle:
		{
			output = HighZ;
			if ( input == 0x81 )
			{
				m_state = State::Command;
				return true;
			}
			return false;
		}

		case State::Command:
		{
			output = m_flag.value;
			switch ( input )
			{
				case 'R':	m_state = State::Read_ID1;		return true;
				case 'W':	m_state = State::Write_ID1;		return true;
				case 'S':	m_state = State::ID_ID1;		return true;

				// Transfer aborts immediately after the faulty command byte, or, occasionally after one more byte (with response FFh to that extra byte).
				default:	m_state = State::Idle;			return false;
			}
		}

		case State::Read_ID1:
		case State::Write_ID1:
		case State::ID_ID1:
		{
			output = 0x5a;
			m_state = IncrementEnum( m_state );
			return true;
		}

		case State::Read_ID2:
		case State::Write_ID2:
		case State::ID_ID2:
		{
			output = 0x5d;
			m_state = IncrementEnum( m_state );
			return true;
		}

		case State::Read_AddressHigh:
		case State::Write_AddressHigh:
		{
			output = 0;
			m_previousData = input;
			m_address = ( static_cast<uint16_t>( input ) << 8 ) | ( m_address & 0xff );
			m_state = IncrementEnum( m_state );
			return true;
		}

		case State::Read_AddressLow:
		case State::Write_AddressLow:
		{
			output = std::exchange( m_previousData, input );
			m_address = ( m_address & 0xff00 ) | static_cast<uint16_t>( input );
			m_state = IncrementEnum( m_state );
			return true;
		}

		case State::Read_CommandAck1:
		case State::Write_CommandAck1:
		case State::ID_CommandAck1:
		{
			output = 0x5c;
			m_state = IncrementEnum( m_state );
			return true;
		}

		case State::Read_CommandAck2:
		case State::Write_CommandAck2:
		case State::ID_CommandAck2:
		{
			output = 0x5d;
			m_state = IncrementEnum( m_state );
			return true;
		}

		case State::Read_ConfirmAddressHigh:
		{
			output = ( m_address < SectorCount ) ? static_cast<uint8_t>( m_address >> 8 ) : HighZ;
			m_state = State::Read_ConfirmAddressLow;
			return true;
		}

		case State::Read_ConfirmAddressLow:
		{
			if ( m_address < SectorCount )
			{
				output = static_cast<uint8_t>( m_address );
				m_state = State::Read_Data;
				return true;
			}
			else
			{
				output = HighZ;
				m_state = State::Idle;
				return false;
			}
		}

		case State::Read_Data:
		{
			dbAssert( m_address < SectorCount );
			output = m_memory[ m_address * SectorSize + m_dataCount ];

			if ( ++m_dataCount == SectorSize )
			{
				m_dataCount = 0;
				m_state = State::Read_Checksum;
			}

			return true;
		}

		case State::Read_Checksum:
		{
			output = GetCurrentSectorChecksum();
			m_state = State::Read_EndByte;
			return true;
		}

		case State::Read_EndByte:
		{
			output = Good;
			m_state = State::Idle;
			return false;
		}

		case State::Write_Data:
		{
			output = std::exchange( m_previousData, input );

			if ( m_dataCount == 0 )
				m_flag.directoryNotRead = false;

			if ( m_address < SectorCount )
			{
				auto& byte = m_memory[ m_address * SectorSize + m_dataCount ];
				m_written |= ( byte != input );
				byte = input;
			}
			
			if ( ++m_dataCount == SectorSize )
			{
				m_dataCount = 0;
				m_state = State::Write_Checksum;
			}

			return true;
		}

		case State::Write_Checksum:
		{
			output = m_previousData;
			m_writeChecksum = input;
			m_state = State::Write_CommandAck1;
			return true;
		}

		case State::Write_EndByte:
		{
			if ( m_address >= SectorCount )
				output = BadSector;
			else if ( m_writeChecksum != GetCurrentSectorChecksum() )
				output = BadChecksum;
			else
				output = Good;

			m_state = State::Idle;
			return false;
		}

		case State::ID_SectorCountHigh:
		{
			output = static_cast<uint8_t>( SectorCount >> 8 );
			m_state = State::ID_SectorCountLow;
			return true;
		}

		case State::ID_SectorCountLow:
		{
			output = static_cast<uint8_t>( SectorCount );
			m_state = State::ID_SectorSizeHigh;
			return true;
		}

		case State::ID_SectorSizeHigh:
		{
			output = static_cast<uint8_t>( SectorSize >> 8 );
			m_state = State::ID_SectorSizeLow;
			return true;
		}

		case State::ID_SectorSizeLow:
		{
			output = static_cast<uint8_t>( SectorSize );
			m_state = State::Idle;
			return false;
		}
	}

	dbBreak();
	output = HighZ;
	return false;
}

uint8_t MemoryCard::GetCurrentSectorChecksum() const
{
	dbAssert( m_address < SectorCount );
	const uint8_t* first = m_memory.data() + m_address * SectorSize;
	return XorChecksum( first, SectorSize ) ^ static_cast<uint8_t>( m_address ) ^ static_cast<uint8_t>( m_address >> 8 );
}

void MemoryCard::Format()
{
	uint8_t* data = m_memory.data();

	new( data ) HeaderFrame();

	for ( size_t i = 1; i < 16; ++i )
		new( data + i * SectorSize ) DirectoryFrame();

	for ( size_t i = 16; i < 36; ++i )
		new( data + i * SectorSize ) BrokenSectorList();

	// unused frames and test write frame
	std::fill( data + 36 * SectorSize, data + 63 * SectorSize, uint8_t( 0xff ) );

	new( data + 63 * SectorSize ) HeaderFrame();

	// title, icon, and data frames
	std::fill( data + 64 * SectorSize, data + SectorCount * SectorSize, uint8_t( 0x00 ) );
}

bool MemoryCard::Save()
{
	if ( m_filename.empty() )
		return false;

	std::ofstream fout( m_filename, std::ios::binary );
	if ( !fout.is_open() )
		return false;

	fout.write( (const char*)m_memory.data(), TotalSize );
	fout.close();

	m_written = false;
	return true;
}


void MemoryCard::Serialize( SaveStateSerializer& serializer )
{
	serializer( m_state );
	serializer( m_flag.value );
	serializer( m_dataCount );
	serializer( m_address );
	serializer( m_previousData );
	serializer( m_writeChecksum );
}

}