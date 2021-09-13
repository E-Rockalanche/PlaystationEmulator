#pragma once

#include "CDRom.h"
#include "FifoBuffer.h"

#include <stdx/assert.h>
#include <stdx/bit.h>

#include <cstdint>

namespace PSX
{

class CycleScheduler;
class InterruptControl;

class CDRomDrive
{
public:
	CDRomDrive( InterruptControl& interruptControl, CycleScheduler& cycleScheduler );

	void Reset();

	template <typename T>
	T ReadDataFifo() noexcept
	{
		T value = 0;

		for ( int i = 0; i < sizeof( T ); ++i )
			value |= static_cast<T>( m_dataBuffer.Pop() ) << ( i * 8 );

		return value;
	}

	uint8_t Read( uint32_t index ) noexcept;
	void Write( uint32_t index, uint8_t value ) noexcept;

	void AddCycles( uint32_t cycles ) noexcept;
	uint32_t GetCyclesUntilCommand() const noexcept;

	void SetCDRom( std::unique_ptr<CDRom> cdrom )
	{
		m_cdrom = std::move( cdrom );
	}

	bool CanReadDisk() const noexcept
	{
		return m_cdrom != nullptr;
	}

private:
	static constexpr uint32_t DataBufferSize = CDRom::RawBytesPerSector - CDRom::SyncSize;
	static constexpr uint32_t ParamaterBufferSize = 16;
	static constexpr uint32_t ResponseBufferSize = 16;
	static constexpr uint32_t NumSectorBuffers = 8;

	enum class Command : uint8_t
	{
		Invalid = 0x00, // reportedly "Sync"

		GetStat = 0x01,
		SetLoc = 0x02, // amm, ass, asect
		Play = 0x03, // track
		Forward = 0x04,
		Backward = 0x05,
		ReadN = 0x06,
		MotorOn = 0x07,
		Stop = 0x08,
		Pause = 0x09,
		Init = 0x0a,
		Mute = 0x0b,
		Demute = 0x0c,
		SetFilter = 0x0d, // file, channel
		SetMode = 0x0e, // mode
		GetParam = 0x0f,
		GetLocL = 0x10,
		GetLocP = 0x11,
		SetSession = 0x12, // session
		GetTrackNumber = 0x13,
		GetTD = 0x14, // track (BCD)
		SeekL = 0x15,
		SeekP = 0x16,

		Test = 0x19, // sub_function
		GetID = 0x1a,
		ReadS = 0x1b,
		ResetDrive = 0x1c,

		Secret1 = 0x50,
		Secret2 = 0x51, // "Licensed by"
		Secret3 = 0x52, // "Sony"
		Secret4 = 0x53, // "Computer"
		Secret5 = 0x54, // "Entertainment"
		Secret6 = 0x55, // "<region>"
		Secret7 = 0x56,
		SecretLock = 0x57,

		// 0x58-0x5f crashes the HC05 (jumps into a data area)
	};

	struct Status
	{
		enum : uint8_t
		{
			Error = 1u,
			SpindleMotor = 1u << 1,
			SeekError = 1u << 2,
			IdError = 1u << 3,
			ShellOpen = 1u << 4, // 1=is/was open

			// only one of these bits can be set at a time
			Read = 1u << 5,
			Seek = 1u << 6,
			Play = 1u << 7
		};
	};

	struct ControllerMode
	{
		enum : uint8_t
		{
			CDDA = 1u << 0, // 1=Allow to Read CD-DA Sectors; ignore missing EDC
			AutoPause = 1 << 1, // 1=Auto Pause upon End of Track
			Report = 1u << 2, // 1=Enable Report-Interrupts for Audio Play
			XAFilter = 1u << 3, // 1=Process only XA-ADPCM sectors that match Setfilter
			IgnoreBit = 1u << 4, // 1=Ignore Sector Size and Setloc position
			SectorSize = 1u << 5, // 0=800h=DataOnly, 1=924h=WholeSectorExceptSyncBytes
			XAADPCM = 1u << 6, // 0=Off, 1=Send XA-ADPCM sectors to SPU Audio Input
			DoubleSpeed = 1u << 7 // 0=Normal speed, 1=Double speed
		};
	};

	enum class ErrorCode
	{
		InvalidArgument = 0x10,
		WrongNumberOfParameters = 0x20,
		InvalidCommand = 0x40,
		CannotRespondYet = 0x80,
		SeekFailed = 0x04,
		DriveDoorOpened = 0x08
	};

	struct InterruptResponse
	{
		enum : uint8_t
		{
			None = 0x00,
			ReceivedData = 0x01,
			Second = 0x02,
			First = 0x03,
			DataEnd = 0x04,
			Error = 0x05,

			// command start can be or'd with the above responses
			CommandStart = 0x10
		};
	};

private:
	void SendCommand( Command command ) noexcept;
	void ExecuteCommand( Command command ) noexcept;
	void ExecuteSecondResponse( Command command ) noexcept;
	void QueueSecondResponse( Command command, int32_t ticks ) noexcept;
	void CheckPendingCommand() noexcept;
	void CheckInterrupt() noexcept;
	void ShiftQueuedInterrupt() noexcept;
	void AbortCommands() noexcept;

	void LoadDataFifo() noexcept;

	// send status and interrupt
	void SendResponse( uint8_t response = InterruptResponse::First )
	{
		m_responseBuffer.Push( m_status );
		m_interruptFlags = response;
	}

	// queue status and second interrupt
	void SendSecondResponse( uint8_t response = InterruptResponse::Second )
	{
		m_secondResponseBuffer.Push( m_status );
		m_queuedInterrupt = response;
	}

	// send status, error code, and interrupt
	void SendError( ErrorCode errorCode )
	{
		m_responseBuffer.Push( m_status | Status::Error );
		m_responseBuffer.Push( static_cast<uint8_t>( errorCode ) );
		m_interruptFlags = InterruptResponse::Error;
	}

	// queue status, error code, and interrupt
	void SendSecondError( ErrorCode errorCode )
	{
		m_secondResponseBuffer.Push( m_status | Status::Error );
		m_secondResponseBuffer.Push( static_cast<uint8_t>( errorCode ) );
		m_queuedInterrupt = InterruptResponse::Error;
	}

	uint32_t GetReadCycles() const noexcept
	{
		const uint32_t cyclesPerSecond = 44100 * 0x300;
		return ( m_mode & ControllerMode::DoubleSpeed ) ? ( cyclesPerSecond / 150 ) : ( cyclesPerSecond / 75 );
	}

	uint32_t GetFirstResponseCycles( Command command ) const noexcept
	{
		// timing taken from duckstation
		return ( command == Command::Init )
			? 120000
			: ( CanReadDisk() ? 25000 : 15000 );
	}

	void ClearSectorBuffers() noexcept
	{
		for ( auto& sector : m_sectorBuffers )
			sector.size = 0;
	}

	bool CommandTransferBusy() const noexcept
	{
		return m_pendingCommand != Command::Invalid;
	}

private:
	InterruptControl& m_interruptControl;
	CycleScheduler& m_cycleScheduler;

	std::unique_ptr<CDRom> m_cdrom;

	uint8_t m_index = 0;
	uint8_t m_interruptEnable = 0;
	uint8_t m_interruptFlags = 0;
	uint8_t m_queuedInterrupt = 0;

	// timing
	Command m_pendingCommand = Command::Invalid;
	Command m_secondResponseCommand = Command::Invalid;
	uint32_t m_cyclesUntilCommand = 0;
	uint32_t m_cyclesUntilSecondResponse = 0;

	uint8_t m_status = 0;

	uint8_t m_file = 0;
	uint8_t m_channel = 0;
	uint8_t m_mode = 0;

	uint8_t m_track = 0;
	uint8_t m_trackIndex = 0; // or just m_index?
	CDRom::Location m_trackLocation;
	CDRom::Location m_seekLocation;

	uint32_t m_currentSector = 0;

	uint8_t m_firstTrack = 0;
	uint8_t m_lastTrack = 0;

	bool m_muteADPCM = false;
	bool m_motorOn = false;

	FifoBuffer<uint8_t, ParamaterBufferSize> m_parameterBuffer;
	FifoBuffer<uint8_t, ResponseBufferSize> m_responseBuffer;
	FifoBuffer<uint8_t, ResponseBufferSize> m_secondResponseBuffer;
	FifoBuffer<uint8_t, DataBufferSize> m_dataBuffer;

	struct SectorBuffer
	{
		size_t size = 0;
		std::array<uint8_t, DataBufferSize> bytes;
	};

	std::array<SectorBuffer, NumSectorBuffers> m_sectorBuffers;
	uint32_t m_readSectorBuffer = 0;
	uint32_t m_writeSectorBuffer = 0;
};

}