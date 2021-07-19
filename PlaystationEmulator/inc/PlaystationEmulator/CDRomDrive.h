#pragma once

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
	enum class Command : uint8_t
	{
		Invalid = 0x00, // reprtedly "Sync"

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
		GetTN = 0x13,
		GetTD = 0x14, // track (BCD)
		SeekL = 0x15,
		SeekP = 0x16,

		Test = 0x19, // sub_function
		GetID = 0x1a,
		ReadS = 0x1b,
		ResetDrive = 0x1c,
		GetQ = 0x1d, // adr, point
		ReadTOC = 0x1e,
		VideoCD = 0x1f, // sub, a, b, c, d, e

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

	CDRomDrive( InterruptControl& interruptControl, CycleScheduler& cycleScheduler );

	void Reset();

	template <typename T>
	T ReadDataFifo() noexcept
	{
		// TODO
		dbBreakMessage( "CDRomDrive::ReadDataFifo()" );
		return 0;
	}

	uint8_t Read( uint32_t index ) noexcept;
	void Write( uint32_t index, uint8_t value ) noexcept;

	void AddCycles( uint32_t cycles ) noexcept;
	uint32_t GetCyclesUntilCommand() const noexcept;

private:
	void SendCommand( Command command ) noexcept;
	void ExecuteCommand( Command command ) noexcept;
	void ExecuteSecondResponse( Command command ) noexcept;
	void QueueSecondResponse( Command command, int32_t ticks ) noexcept;
	void CheckInterrupt() noexcept;
	void ShiftQueuedInterrupt() noexcept;
	
	// CDROM commands
	void GetStat() noexcept;
	void SetLoc() noexcept;
	void Play() noexcept;
	void Forward() noexcept;
	void Backward() noexcept;
	void ReadN() noexcept;
	void MotorOn() noexcept;
	void Stop() noexcept;
	void Pause() noexcept;
	void Init() noexcept;
	void Mute() noexcept;
	void Demute() noexcept;
	void SetFilter() noexcept;
	void SetMode() noexcept;
	void GetParam() noexcept;
	void GetLocL() noexcept;
	void GetLocP() noexcept;
	void SetSession() noexcept;
	void GetTN() noexcept;
	void GetTD() noexcept;
	void SeekL() noexcept;
	void SeekP() noexcept;
	void Test() noexcept;
	void GetID() noexcept;
	void ReadS() noexcept;
	void ResetDrive() noexcept;
	void GetQ() noexcept;
	void ReadTOC() noexcept;
	void VideoCD() noexcept;
	void Secret1() noexcept;
	void Secret2() noexcept;
	void Secret3() noexcept;
	void Secret4() noexcept;
	void Secret5() noexcept;
	void Secret6() noexcept;
	void Secret7() noexcept;
	void SecretLock() noexcept;

private:
	InterruptControl& m_interruptControl;
	CycleScheduler& m_cycleScheduler;

	uint8_t m_index = 0;
	uint8_t m_interruptEnable = 0;
	uint8_t m_interruptFlags = 0;
	uint8_t m_queuedInterrupt = 0;

	// timing
	Command m_pendingCommand = Command::Invalid;
	Command m_pendingSecondResponseCommand = Command::Invalid;
	uint32_t m_cyclesUntilCommand = 0;
	uint32_t m_cyclesUntilSecondResponse = 0;

	bool m_commandTransferBusy = false;

	uint8_t m_status = 0;

	uint8_t m_file = 0;
	uint8_t m_channel = 0;
	uint8_t m_mode = 0;

	uint8_t m_track = 0;
	uint8_t m_trackIndex = 0; // or just m_index?
	uint8_t m_trackMinutes = 0;
	uint8_t m_trackSeconds = 0;
	uint8_t m_trackSector = 0;
	uint8_t m_diskMinutes = 0;
	uint8_t m_diskSeconds = 0;
	uint8_t m_diskSector = 0;

	uint8_t m_firstTrack = 0;
	uint8_t m_lastTrack = 0;

	bool m_wantCommand = false;
	bool m_wantData = false;
	bool m_muteADPCM = false;

	bool m_motorOn = false;

	FifoBuffer<uint8_t, 16> m_parameterBuffer;
	FifoBuffer<uint8_t, 16> m_responseBuffer;
	FifoBuffer<uint8_t, 16> m_secondResponseBuffer;

	FifoBuffer<uint8_t, 32 * 1024> m_dataBuffer;
};

}