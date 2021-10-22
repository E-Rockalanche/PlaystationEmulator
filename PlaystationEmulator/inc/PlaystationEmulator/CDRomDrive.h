#pragma once

#include "Defs.h"

#include "CDRom.h"
#include "FifoBuffer.h"

#include <stdx/bit.h>

namespace PSX
{

class CDRomDrive
{
public:
	CDRomDrive( InterruptControl& interruptControl, EventManager& eventManager );
	~CDRomDrive();

	void Reset();

	// for software 8bit & 16bit reads and DMA 32bit reads
	template <typename T>
	T ReadDataFifo() noexcept;

	uint8_t Read( uint32_t index ) noexcept;
	void Write( uint32_t index, uint8_t value ) noexcept;

	void SetCDRom( std::unique_ptr<CDRom> cdrom );

	bool CanReadDisk() const noexcept
	{
		return m_cdrom != nullptr;
	}

private:
	static constexpr uint32_t DataBufferSize = CDRom::BytesPerSector - CDRom::SyncSize;
	static constexpr uint32_t ParamaterBufferSize = 16;
	static constexpr uint32_t ResponseBufferSize = 16;
	static constexpr uint32_t NumSectorBuffers = 8;

	enum class DriveState
	{
		Idle,
		StartingMotor,
		Seeking,
		Reading,
		ReadingNoRetry,
		Playing,
		ChangingSession
	};

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
		Reset = 0x1c,
		GetQ = 0x1d,
		ReadTOC = 0x1e,

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

	union Status
	{
		Status() : value{ 0 } {}
		struct
		{
			uint8_t error : 1;
			uint8_t motorOn : 1; // spinning up is off
			uint8_t seekError : 1;
			uint8_t idError : 1;
			uint8_t shellOpen : 1;
			uint8_t read : 1;
			uint8_t seek : 1;
			uint8_t play : 1;
		};
		uint8_t value;
	};

	union ControllerMode
	{
		ControllerMode() : value{ 0 } {}
		struct
		{
			uint8_t cdda : 1;			// 1=Allow to Read CD-DA Sectors; ignore missing EDC
			uint8_t autoPause : 1;		// 1=Auto Pause upon End of Track
			uint8_t report : 1;			// 1=Enable Report-Interrupts for Audio Play
			uint8_t xaFilter : 1;		// 1=Process only XA-ADPCM sectors that match Setfilter
			uint8_t ignoreBit : 1;		// 1=Ignore Sector Size and Setloc position
			uint8_t sectorSize : 1;		// 0=800h=DataOnly, 1=924h=WholeSectorExceptSyncBytes
			uint8_t xaadpcm : 1;		// 0=Off, 1=Send XA-ADPCM sectors to SPU Audio Input
			uint8_t doubleSpeed : 1;	// 0=Normal speed, 1=Double speed
		};
		uint8_t value;
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
	// event callbacks
	void ExecuteCommand() noexcept;
	void ExecuteSecondResponse() noexcept;
	void ExecuteDrive() noexcept;

	void SendCommand( Command command ) noexcept;
	void QueueSecondResponse( Command command, cycles_t cycles ) noexcept;
	void ScheduleDriveEvent( DriveState driveState, cycles_t cycles ) noexcept;

	void CheckPendingCommand() noexcept;
	void CheckInterrupt() noexcept;
	void ShiftQueuedInterrupt() noexcept;

	void StartMotor() noexcept;
	void StopMotor() noexcept;
	void BeginSeeking() noexcept;
	void BeginReading() noexcept;
	void LoadDataFifo() noexcept;

	// send status and interrupt
	void SendResponse( uint8_t response = InterruptResponse::First )
	{
		dbAssert( m_interruptFlags == InterruptResponse::None );
		m_responseBuffer.Push( m_status.value );
		m_interruptFlags = response;
	}

	// queue status and second interrupt
	void SendSecondResponse( uint8_t response = InterruptResponse::Second )
	{
		if ( m_queuedInterrupt != InterruptResponse::None )
			dbLogWarning( "CDRomDrive::SendSecondResponse -- overwriting queued interrupt [%u] with new interrupt [%u]", m_queuedInterrupt, response );

		m_secondResponseBuffer.Push( m_status.value );
		m_queuedInterrupt = response;
	}

	// send status, error code, and interrupt
	void SendError( ErrorCode errorCode )
	{
		dbLog( "CDRomDrive::SendError -- [%u]", uint32_t( errorCode ) );
		m_responseBuffer.Push( m_status.value | 0x01 );
		m_responseBuffer.Push( static_cast<uint8_t>( errorCode ) );
		m_interruptFlags = InterruptResponse::Error;
	}

	// queue status, error code, and interrupt
	void SendSecondError( ErrorCode errorCode )
	{
		dbLog( "CDRomDrive::SendSecondError -- [%u]", uint32_t( errorCode ) );
		m_secondResponseBuffer.Push( m_status.value | 0x01 );
		m_secondResponseBuffer.Push( static_cast<uint8_t>( errorCode ) );
		m_queuedInterrupt = InterruptResponse::Error;
	}

	cycles_t GetReadCycles() const noexcept
	{
		return ( m_mode.doubleSpeed ) ? ( CpuCyclesPerSecond / 150 ) : ( CpuCyclesPerSecond / 75 );
	}

	cycles_t GetSeekCycles() const noexcept
	{
		return 20000; // TODO: account for motor spin up time, sector difference, etc
	}

	cycles_t GetFirstResponseCycles( Command command ) const noexcept
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

	bool IsSeeking() const noexcept
	{
		return m_driveState == DriveState::Seeking;
	}

private:
	InterruptControl& m_interruptControl;
	EventHandle m_commandEvent;
	EventHandle m_secondResponseEvent;
	EventHandle m_driveEvent;

	DriveState m_driveState = DriveState::Idle;

	std::unique_ptr<CDRom> m_cdrom;

	uint8_t m_index = 0;
	uint8_t m_interruptEnable = 0;
	uint8_t m_interruptFlags = 0;
	uint8_t m_queuedInterrupt = 0;

	// timing
	Command m_pendingCommand = Command::Invalid;
	Command m_secondResponseCommand = Command::Invalid;

	Status m_status;
	ControllerMode m_mode;

	// XA-ADCPM
	uint8_t m_xaFile = 0;
	uint8_t m_xaChannel = 0;

	uint8_t m_track = 0;
	uint8_t m_trackIndex = 0; // or just m_index?
	CDRom::Location m_trackLocation;
	CDRom::Location m_seekLocation;

	uint8_t m_firstTrack = 0;
	uint8_t m_lastTrack = 0;

	bool m_muteADPCM = false;

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

	// async flags
	bool m_pendingSeek = false; // SetLoc was called, but we haven't called seek yet
	bool m_pendingRead = false; // Read was called, but we were still seeking
};

template <typename T>
inline T CDRomDrive::ReadDataFifo() noexcept
{
	T result = static_cast<T>( m_dataBuffer.Pop() );

	if constexpr ( sizeof( T ) >= 2 )
		result |= static_cast<T>( m_dataBuffer.Pop() << 8 );

	if constexpr ( sizeof( T ) >= 4 )
	{
		result |= static_cast<T>( m_dataBuffer.Pop() << 16 );
		result |= static_cast<T>( m_dataBuffer.Pop() << 24 );
	}

	return result;
}

}