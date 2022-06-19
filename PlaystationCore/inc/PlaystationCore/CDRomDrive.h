#pragma once

#include "Defs.h"

#include "CDRom.h"
#include "FifoBuffer.h"

#include <stdx/bit.h>

#include <functional>
#include <optional>

namespace PSX
{

class CDRomDrive
{
public:
	CDRomDrive( InterruptControl& interruptControl, EventManager& eventManager );
	~CDRomDrive();

	void SetDma( Dma& dma ) { m_dma = &dma; }

	void Reset();

	void DmaRead( uint32_t* data, uint32_t count );

	uint8_t Read( uint32_t index ) noexcept;
	void Write( uint32_t index, uint8_t value ) noexcept;

	void SetCDRom( std::unique_ptr<CDRom> cdrom );
	CDRom* GetCDRom() { return m_cdrom.get(); }

	bool CanReadDisk() const noexcept
	{
		return m_cdrom != nullptr;
	}

	STDX_forceinline std::pair<int16_t, int16_t> GetAudioFrame()
	{
		const uint32_t frame = m_audioBuffer.Empty() ? 0 : m_audioBuffer.Pop();
		const int16_t left = static_cast<int16_t>( frame );
		const int16_t right = static_cast<int16_t>( frame >> 16 );
		const int16_t leftResult = SaturateSample( ApplyVolume( left, m_volumes.leftToLeft ) + ApplyVolume( right, m_volumes.rightToLeft ) );
		const int16_t rightResult = SaturateSample( ApplyVolume( right, m_volumes.rightToRight ) + ApplyVolume( left, m_volumes.leftToRight ) );
		return std::make_pair( leftResult, rightResult );
	}

	void Serialize( SaveStateSerializer& serializer );

private:
	static constexpr uint32_t DataBufferSize = CDRom::BytesPerSector - CDRom::SyncSize;
	static constexpr uint32_t ParamaterBufferSize = 16;
	static constexpr uint32_t ResponseBufferSize = 16;
	static constexpr uint32_t NumSectorBuffers = 8;

	static constexpr uint32_t XaAdpcmSampleBufferSize = CDXA::AdpcmChunks * CDXA::AdpcmWordsPerChunk * 8; // 8 nibbles per word
	static constexpr uint32_t ResampleRingBufferSize = 0x20;

	static constexpr uint32_t AudioFifoSize = 44100; // 1 second of audio

	static constexpr cycles_t MotorStartCycles = CpuCyclesPerSecond;
	static constexpr cycles_t GetIdCycles = 33868; // number from Duckstation
	static constexpr cycles_t SpeedupCycles = static_cast<cycles_t>( 0.8 * CpuCyclesPerSecond );
	static constexpr cycles_t SlowdownCycles = static_cast<cycles_t>( CpuCyclesPerSecond );
	static constexpr cycles_t ReadTOCCycles = CpuCyclesPerSecond / 2;

	static const std::array<uint8_t, 256> ExpectedCommandParameters;

	union Status
	{
		struct
		{
			uint8_t index : 2;
			uint8_t adpBusy : 1;
			uint8_t parameterFifoEmpty : 1;

			uint8_t parameterFifoNotFull : 1;
			uint8_t responseFifoNotEmpty : 1;
			uint8_t dataFifoNotEmpty : 1;
			uint8_t commandTransferBusy : 1;
		};
		uint8_t value = 0;
	};

	enum class DriveState
	{
		Idle,
		StartingMotor,
		SeekingLogical,
		SeekingPhysical,
		Reading,
		ReadingNoRetry,
		Playing,
		ChangingSession,
		ChangingSpeedOrReadingTOC,
		OpeningShell,
	};

	enum class Command : uint8_t
	{
		// 0x00 - reprtedly "Sync"

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

		// 0x17 - reprtedly "SetClock"
		// 0x18 - reprtedly "GetClock"

		Test = 0x19, // sub_function
		GetID = 0x1a,
		ReadS = 0x1b,
		Reset = 0x1c,
		GetQ = 0x1d,
		ReadTOC = 0x1e,

		// 0x1f-0x4f - invalid

		Secret1 = 0x50,
		Secret2 = 0x51, // "Licensed by"
		Secret3 = 0x52, // "Sony"
		Secret4 = 0x53, // "Computer"
		Secret5 = 0x54, // "Entertainment"
		Secret6 = 0x55, // "<region>"
		Secret7 = 0x56,
		SecretLock = 0x57,

		// 0x58-0x5f crashes the HC05 (jumps into a data area)

		// 0x6f-0xff - invalid
	};

	union DriveStatus
	{
		struct
		{
			uint8_t : 1;
			uint8_t motorOn : 1; // spinning up is off
			uint8_t : 2;

			uint8_t shellOpen : 1;
			uint8_t read : 1;
			uint8_t seek : 1;
			uint8_t play : 1;
		};
		uint8_t value = 0;
	};

	struct DriveStatusError
	{
		enum : uint8_t
		{
			Error = 1u << 0,
			SeekError = 1u << 2,
			IdError = 1u << 3
		};
	};

	union ControllerMode
	{
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
		uint8_t value = 0;
	};

	enum class ErrorCode : uint8_t
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

	struct ChannelVolumes
	{
		uint8_t leftToLeft = 0x80;
		uint8_t leftToRight = 0;
		uint8_t rightToRight = 0x80;
		uint8_t rightToLeft = 0;
	};

private:
	void UpdateStatus() noexcept;
	void UpdateCommandEvent() noexcept;

	// events
	void ExecuteCommand() noexcept;
	void ExecuteCommandSecondResponse() noexcept;
	void ExecuteDriveState() noexcept;
	void SendCommand( Command command ) noexcept;
	void QueueSecondResponse( Command command, cycles_t cycles ) noexcept;
	void ScheduleDriveEvent( DriveState driveState, cycles_t cycles ) noexcept;

	// interrupts
	void SendStatusAndInterrupt( uint8_t response = InterruptResponse::First ) noexcept;
	void SetAsyncInterrupt( uint8_t response = InterruptResponse::Second ) noexcept;
	void SendAsyncStatusAndInterrupt( uint8_t response = InterruptResponse::Second ) noexcept;
	void ClearAsyncInterrupt() noexcept;
	void SendError( ErrorCode errorCode, uint8_t statusErrorBits = DriveStatusError::Error ) noexcept;
	void SendAsyncError( ErrorCode errorCode, uint8_t statusErrorBits = DriveStatusError::Error ) noexcept;
	void CheckInterrupt() noexcept;
	void ShiftQueuedInterrupt() noexcept;

	// drive
	void ResetDriveState() noexcept;
	void StartMotor() noexcept;
	void StopMotor() noexcept;
	void BeginSeeking( bool logical ) noexcept;
	void BeginReading() noexcept;
	void BeginPlaying( uint8_t track ) noexcept;
	void RequestData() noexcept;

	cycles_t GetReadCycles() const noexcept
	{
		return static_cast<cycles_t>( CpuCyclesPerSecond / ( CDRom::SectorsPerSecond * ( 1 + m_mode.doubleSpeed ) ) );
	}

	cycles_t GetSpeedChangeCycles() const noexcept
	{
		return m_mode.doubleSpeed ? SpeedupCycles : SlowdownCycles;
	}

	void UpdatePositionWhileSeeking() noexcept;

	cycles_t GetSeekCycles( CDRom::LogicalSector seekposition ) const noexcept;

	cycles_t GetFirstResponseCycles( Command command ) const noexcept;

	void ClearSectorBuffers() noexcept
	{
		for ( auto& sector : m_sectorBuffers )
			sector.size = 0;
	}

	bool IsSeeking() const noexcept { return m_driveState == DriveState::SeekingLogical || m_driveState == DriveState::SeekingPhysical; }
	bool IsReading() const noexcept { return m_driveState == DriveState::Reading || m_driveState == DriveState::ReadingNoRetry; }
	bool IsPlaying() const noexcept { return m_driveState == DriveState::Playing; }

	// returns true if seek was successful
	bool CompleteSeek( bool logical ) noexcept;

	void ResetAudioDecoder() noexcept;

	void SendDataEndResponse() noexcept;

	void ProcessDataSector( const CDRom::Sector& sector );
	void ProcessCDDASector( const CDRom::Sector& sector );
	void DecodeAdpcmSector( const CDRom::Sector& sector );

	template <bool IsStereo, bool HalfSampleRate>
	void ResampleXaAdpcm( const int16_t* samples, uint32_t count );

	void AddAudioFrame( int16_t left, int16_t right )
	{
		const uint32_t frame = static_cast<uint16_t>( left ) | ( static_cast<uint32_t>( static_cast<uint16_t>( right ) ) << 16 );
		m_audioBuffer.Push( frame );
	}

	static constexpr int32_t ApplyVolume( int16_t sample, uint8_t volume ) noexcept
	{
		return ( static_cast<int32_t>( sample ) * static_cast<int32_t>( volume ) ) >> 7;
	}

	static constexpr int16_t SaturateSample( int32_t sample )
	{
		constexpr int32_t Min = std::numeric_limits<int16_t>::min();
		constexpr int32_t Max = std::numeric_limits<int16_t>::max();
		return static_cast<int16_t>( ( sample < Min ) ? Min : ( sample > Max ) ? Max : sample );
	}

private:
	InterruptControl& m_interruptControl;
	Dma* m_dma = nullptr;

	std::unique_ptr<CDRom> m_cdrom;

	EventHandle m_commandEvent;
	EventHandle m_secondResponseEvent;
	EventHandle m_driveEvent;

	DriveState m_driveState = DriveState::Idle;

	Status m_status;
	uint8_t m_interruptEnable = 0;
	uint8_t m_interruptFlags = 0;
	uint8_t m_queuedInterrupt = 0;

	ChannelVolumes m_volumes;
	ChannelVolumes m_nextVolumes;

	// timing
	std::optional<Command> m_pendingCommand;
	std::optional<Command> m_secondResponseCommand;

	DriveStatus m_driveStatus;
	ControllerMode m_mode;

	struct XaFile
	{
		uint8_t file = 0;
		uint8_t channel = 0;
	};

	XaFile m_xaFilter;
	std::optional<XaFile> m_xaCurrent;

	CDRom::SubQ m_lastSubQ;

	uint8_t m_playingTrackNumberBCD = 0;
	uint8_t m_secondResponseParameter = 0;

	bool m_muted = false;
	bool m_muteADPCM = false;

	FifoBuffer<uint8_t, ParamaterBufferSize> m_parameterBuffer;
	FifoBuffer<uint8_t, ResponseBufferSize> m_responseBuffer;
	FifoBuffer<uint8_t, ResponseBufferSize> m_secondResponseBuffer;
	FifoBuffer<uint8_t, DataBufferSize> m_dataBuffer;

	struct SectorBuffer
	{
		size_t size = 0;
		std::array<uint8_t, DataBufferSize> bytes{};
	};

	std::array<SectorBuffer, NumSectorBuffers> m_sectorBuffers;
	uint32_t m_readSectorBuffer = 0;
	uint32_t m_writeSectorBuffer = 0;

	struct SectorHeaders
	{
		CDRom::Header header;
		CDXA::SubHeader subHeader;
	};

	std::optional<SectorHeaders> m_currentSectorHeaders;

	CDRom::Location m_seekLocation;

	CDRom::LogicalSector m_currentPosition = 0;
	CDRom::LogicalSector m_seekStart = 0;
	CDRom::LogicalSector m_seekEnd = 0;

	// async flags
	bool m_pendingSeek = false; // SetLoc was called, but we haven't called seek yet
	bool m_pendingRead = false; // Read was called, but we need to seek
	bool m_pendingPlay = false; // Play was called, but we need to seek

	FifoBuffer<uint32_t, AudioFifoSize> m_audioBuffer;
	std::array<int32_t, 4> m_oldXaAdpcmSamples{};
	std::array<std::array<int16_t, ResampleRingBufferSize>, 2> m_resampleRingBuffers{};
	uint8_t m_resampleP = 0;

	// not serialized
	std::unique_ptr<int16_t[]> m_xaAdpcmSampleBuffer;
};

}