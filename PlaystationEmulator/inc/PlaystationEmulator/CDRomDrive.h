#pragma once

#include "FifoBuffer.h"

#include "assert.h"
#include "bit.h"

#include <cstdint>

namespace PSX
{

class InterruptControl;

class CDRomDrive
{
public:
	CDRomDrive( InterruptControl& interruptControl ) : m_interruptControl{ interruptControl }
	{
		Reset();
	}

	void Reset();

	template <typename T>
	T ReadDataFifo() noexcept
	{
		dbLog( "read CDROM data fifo [width=%i]", sizeof( T ) * 8 );
		return 0; // TODO
	}

	uint8_t Read( uint32_t index ) noexcept;

	void Write( uint32_t index, uint8_t value ) noexcept;

private:
	void ExecuteCommand( uint8_t command ) noexcept;
	
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

	uint8_t m_index;
	uint8_t m_interruptEnable;
	uint8_t m_interruptFlags;

	uint8_t m_queuedInterrupt;

	uint8_t m_status;

	uint8_t m_file;
	uint8_t m_channel;
	uint8_t m_mode;

	uint8_t m_track;
	uint8_t m_trackIndex; // or just m_index?
	uint8_t m_trackMinutes;
	uint8_t m_trackSeconds;
	uint8_t m_trackSector;
	uint8_t m_diskMinutes;
	uint8_t m_diskSeconds;
	uint8_t m_diskSector;

	uint8_t m_firstTrack;
	uint8_t m_lastTrack;

	bool m_wantCommand;
	bool m_wantData;
	bool m_muteADPCM;

	bool m_motorOn;

	FifoBuffer<uint8_t, 16> m_parameterBuffer;
	FifoBuffer<uint8_t, 16> m_responseBuffer;

	FifoBuffer<uint8_t, 32 * 1024> m_dataBuffer;
};

}