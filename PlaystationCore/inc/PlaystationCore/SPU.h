#pragma once

#include "Defs.h"
#include "FifoBuffer.h"
#include "Memory.h"

#include <array>
#include <cstdint>

namespace PSX
{

class Spu
{
public:
	Spu( InterruptControl& interruptControl, EventManager& eventManager );

	void Reset();

	void SetDma( Dma& dma ) { m_dma = &dma; }

	uint16_t Read( uint32_t offset ) noexcept;

	void Write( uint32_t offset, uint16_t value ) noexcept;

	void DmaWrite( const uint32_t* dataIn, uint32_t count ) noexcept;
	void DmaRead( uint32_t* dataOut, uint32_t count ) noexcept;

private:

	enum class TransferMode
	{
		Stop,
		ManualWrite,
		DMAWrite,
		DMARead
	};

	union ADPCMHeader
	{
		struct
		{
			uint8_t loopEnd : 1;
			uint8_t loopRepeat : 1;
			uint8_t loopStart : 1;
			uint8_t : 5;
		};
		uint8_t value = 0;
	};
	static_assert( sizeof( ADPCMHeader ) == 1 );

	union VoiceADSR
	{
		struct
		{
			uint16_t sustainLevel : 4;
			uint16_t decayShift : 4;
			uint16_t attackStep : 2;
			uint16_t attackShift : 5;
			uint16_t attackMode : 1;

			uint16_t releaseShift : 5;
			uint16_t releaseMode : 1;
			uint16_t sustainStep : 2;
			uint16_t sustainShift : 5;
			uint16_t sustainDirection : 1;
			uint16_t sustainMode : 1;
		};
		struct
		{
			uint16_t valueLow;
			uint16_t valueHigh;
		};
		uint32_t value = 0;
	};
	static_assert( sizeof( VoiceADSR ) == 4 );

	union Volume
	{
		struct
		{
			int16_t fixedVolume : 15; // divided by 2
			int16_t : 1;
		};
		struct
		{
			uint16_t sweepStep : 2;
			uint16_t sweepShift : 5;
			uint16_t : 5;
			uint16_t sweepPhase : 1;
			uint16_t sweepDirection : 1;
			uint16_t sweepMode : 1;
			uint16_t sweepVolume : 1; // 0=fixed volume, 1=sweep volume
		};
		uint16_t value = 0;
	};
	static_assert( sizeof( Volume ) == 2 );

	union VoiceRegisters
	{
		VoiceRegisters() : registers{} {}

		struct
		{
			Volume volumeLeft;
			Volume volumeRight;
			uint16_t adpcmSampleRate; // (VxPitch)
			uint16_t adpcmStartAddress; // x8
			VoiceADSR adsr;
			uint16_t currentADSRVolume;
			uint16_t adpcmRepeatAddress; // x8
		};
		std::array<uint16_t, 8> registers;
	};
	static_assert( sizeof( VoiceRegisters ) == 16 );

	struct VoiceFlags
	{
		uint32_t keyOn = 0;
		uint32_t keyOff = 0;
		uint32_t pitchModulationEnable = 0;
		uint32_t noiseModeEnable = 0;
		uint32_t reverbEnable = 0;
		uint32_t status = 0;
	};
	static_assert( sizeof( VoiceFlags ) == 24 );

	union Control
	{
		struct
		{
			uint16_t cdAudioEnable : 1;
			uint16_t externalAudioEnable : 1;
			uint16_t cdAudioReverb : 1;
			uint16_t externalAudioReverb : 1;
			uint16_t soundRamTransferMode : 2;
			uint16_t irqEnable : 1;
			uint16_t reverbMasterEnable : 1;

			uint16_t noiseFrequencyStep : 2;
			uint16_t noiseFrequencyShift : 4;
			uint16_t mute : 1;
			uint16_t enable : 1;
		};
		uint16_t value = 0;

		TransferMode GetTransfermode() const noexcept { return static_cast<TransferMode>( soundRamTransferMode ); }
	};
	static_assert( sizeof( Control ) == 2 );

	/*
	The Transfer Type selects how data is forwarded from Fifo to SPU RAM:
		__Transfer Type___Halfwords in Fifo________Halfwords written to SPU RAM__
		0,1,6,7  Fill     A,B,C,D,E,F,G,H,...,X    X,X,X,X,X,X,X,X,...
		2        Normal   A,B,C,D,E,F,G,H,...,X    A,B,C,D,E,F,G,H,...
		3        Rep2     A,B,C,D,E,F,G,H,...,X    A,A,C,C,E,E,G,G,...
		4        Rep4     A,B,C,D,E,F,G,H,...,X    A,A,A,A,E,E,E,E,...
		5        Rep8     A,B,C,D,E,F,G,H,...,X    H,H,H,H,H,H,H,H,...
	*/
	union DataTransferControl
	{
		struct
		{
			uint16_t : 1;
			uint16_t transferType : 3;
			uint16_t : 12;
		};
		uint16_t value = 0;
	};
	static_assert( sizeof( DataTransferControl ) == 2 );

	union Status
	{
		struct
		{
			// same as SPUCNT.Bit5-0
			uint16_t cdAudioEnable : 1;
			uint16_t externalAudioEnable : 1;
			uint16_t cdAudioReverb : 1;
			uint16_t externalAudioReverb : 1;
			uint16_t soundRamTransferMode : 2;

			uint16_t irq : 1;
			uint16_t dmaRequest : 1;

			uint16_t dmaWriteRequest : 1;
			uint16_t dmaReadRequest : 1;
			uint16_t transferBusy : 1;
			uint16_t writingToCaptureBufferHalf : 1;
			uint16_t : 4;
		};
		uint16_t value = 0;

		static constexpr uint16_t ControlMask = 0x003f;
	};
	static_assert( sizeof( Status ) == 2 );

	union ReverbRegisters
	{
		struct
		{
			uint16_t apfOffset1;
			uint16_t apfOffset2;
			int16_t reflectionVolume1;
			int16_t combVolume1;
			int16_t combVolume2;
			int16_t combVolume3;
			int16_t combVolume4;
			int16_t reflectionVolume2;
			int16_t apfVolume1;
			int16_t apfVolume2;
			uint16_t sameSideReflectionAddress1Left;
			uint16_t sameSideReflectionAddress1Right;
			uint16_t combAddress1Left;
			uint16_t combAddress1Right;
			uint16_t combAddress2Left;
			uint16_t combAddress2Right;
			uint16_t sameSideReflectionAddress2Left;
			uint16_t sameSideReflectionAddress2Right;
			uint16_t differentSideReflectionAddress1Left;
			uint16_t differentSideReflectionAddress1Right;
			uint16_t combAddress3Left;
			uint16_t combAddress3Right;
			uint16_t combAddress4Left;
			uint16_t combAddress4Right;
			uint16_t differentSideReflectionAddress2Left;
			uint16_t differentSideReflectionAddress2Right;
			uint16_t apfAddress1Left;
			uint16_t apfAddress1Right;
			uint16_t apfAddress2Left;
			uint16_t apfAddress2Right;
			int16_t inputVolumeLeft;
			int16_t inputVolumeRight;
		};
		std::array<uint16_t, 32> registers{};
	};
	static_assert( sizeof( ReverbRegisters ) == 64 );

	static constexpr uint32_t VoiceCount = 24;
	static constexpr uint32_t SpuFifoSize = 32;
	static constexpr uint32_t SpuRamSize = 0x80000;
	static constexpr uint32_t SpuRamAddressMask = SpuRamSize - 1;
	static constexpr cycles_t TransferCyclesPerHalfword = 16;

private:
	void SetSpuControl( uint16_t value ) noexcept;

	void UpdateDmaRequest() noexcept;

	void ExecuteManualWrite() noexcept;

	void ScheduleTransferEvent() noexcept;
	void UpdateTransferEvent( cycles_t cycles ) noexcept;

	void TriggerInterrupt() noexcept;

	bool CanTriggerInterrupt() const noexcept { return m_control.irqEnable && !m_status.irq; }
	bool CheckIrqAddress( uint32_t address ) const noexcept { return static_cast<uint32_t>( m_irqAddress * 8 ) == address; }

	void TryTriggerInterrupt( uint32_t address )
	{
		if ( CheckIrqAddress( address ) && CanTriggerInterrupt() )
			TriggerInterrupt();
	}

	void GeneratePendingSamples();

private:
	InterruptControl& m_interruptControl;
	Dma* m_dma = nullptr;

	EventHandle m_transferEvent;

	std::array<VoiceRegisters, VoiceCount> m_voiceRegisters;

	std::array<Volume, 2> m_mainVolume;
	std::array<Volume, 2> m_reverbOutVolume;

	VoiceFlags m_voiceFlags;

	uint16_t m_reverbWorkAreaStartAddress = 0;
	uint16_t m_irqAddress = 0;
	uint16_t m_transferAddressRegister = 0;
	uint16_t m_transferBufferRegister = 0;

	Control m_control;
	DataTransferControl m_dataTransferControl;
	Status m_status;

	std::array<int16_t, 2> m_cdAudioInputVolume; // for normal CD-DA and compressed XA-ADPCM
	std::array<int16_t, 2> m_externalAudioInputVolume;
	std::array<int16_t, 2> m_currentMainVolume;

	ReverbRegisters m_reverb;

	std::array<std::array<Volume, 2>, VoiceCount> m_voiceVolumes;

	std::array<uint16_t, 0x20> m_unknownRegisters{}; // read/write
	
	FifoBuffer<uint16_t, SpuFifoSize> m_transferBuffer;

	uint16_t m_transferAddress = 0;

	Memory<SpuRamSize> m_ram;
};

}