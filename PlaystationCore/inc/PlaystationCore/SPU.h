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

	enum class TransferType
	{
		Fill = 0, // and 1, 6, 7
		Normal = 2,
		Rep2 = 3,
		Rep4 = 4,
		Rep8 = 5
	};

	union Volume
	{
		constexpr Volume() noexcept : channels{} {}
		struct
		{
			int16_t left;
			int16_t right;
		};
		std::array<int16_t, 2> channels;
	};

	union Voice
	{
		constexpr Voice() noexcept : registers{} {}
		struct
		{
			Volume volume;
			uint16_t adpcmSampleRate;
			uint16_t adpcmStartAddress;
			uint32_t adsr; // attack, decay, sustain, release
			uint16_t currentADSRVolume;
			uint16_t adpcmRepeatAddress;
		};
		std::array<uint16_t, 8> registers;
	};
	static_assert( sizeof( Voice ) == 16 );

	struct VoiceFlags
	{
		uint32_t keyOn = 0;
		uint32_t keyOff = 0;
		uint32_t pitchModulationEnable = 0;
		uint32_t noiseModeEnable = 0;
		uint32_t reverbEnable = 0;
		uint32_t status = 0;
	};

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
			// same as SPUCNT.Bit5-0, but, applied a bit delayed
			uint16_t cdAudioEnable : 1;
			uint16_t externalAudioEnable : 1;
			uint16_t cdAudioReverb : 1;
			uint16_t externalAudioReverb : 1;
			uint16_t soundRamTransferMode : 2;

			uint16_t irq : 1;
			uint16_t dmaRequest : 1;

			uint16_t dmaWriteRequest : 1;
			uint16_t dmaReadRequest : 1;
			uint16_t dmaBusy : 1;
			uint16_t writingToCaptureBufferHalf : 1;
			uint16_t : 4;
		};
		uint16_t value = 0;

		static constexpr uint16_t ControlMask = 0x003f;
	};
	static_assert( sizeof( Status ) == 2 );

	struct ReverbRegister
	{
		enum : uint32_t
		{
			OutVolumeLeft,
			OutVolumeRight,
			WorkAreaStartAddress,
			APFOffset1,
			APFOffset2,
			ReflectionVolume1,
			CombVolume1,
			CombVolume2,
			CombVolume3,
			CombVolume4,
			ReflectionVolume2,
			APFVolume1,
			APFVolume2,
			SameSideReflectionAddress1Left,
			SameSideReflectionAddress1Right,
			CombAddress1Left,
			CombAddress1Right,
			CombAddress2Left,
			CombAddress2Right,
			SameSideReflectionAddress2Left,
			SameSideReflectionAddress2Right,
			DifferentSideReflectAddress1Left,
			DifferentSideReflectAddress1Right,
			CombAddress3Left,
			CombAddress3Right,
			CombAddress4Left,
			CombAddress4Right,
			DifferentSideReflectAddress2Left,
			DifferentSideReflectAddress2Right,
			APFAddress1Left,
			APFAddress1Right,
			APFAddress2Left,
			APFAddress2Right,
			InVolumeLeft,
			InVolumeRight,

			NumRegisters
		};
	};

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

private:
	// meaningful registers range from 0x1F801C00 to 0x1F801E5f

	InterruptControl& m_interruptControl;
	Dma* m_dma = nullptr;

	EventHandle m_transferEvent;

	union
	{
		std::array<Voice, VoiceCount> m_voices{};
		std::array<uint16_t, VoiceCount * 8> m_voiceRegisters;
	};

	union
	{
		std::array<Volume, VoiceCount> m_voiceVolumes;
		std::array<uint16_t, VoiceCount * 2> m_voiceVolumeRegisters;
	};

	Volume m_mainVolume;
	Volume m_reverbOutVolume;
	
	VoiceFlags m_voiceFlags;

	Volume m_cdAudioInputVolume; // for normal CD-DA and compressed XA-ADPCM
	Volume m_externalAudioInputVolume;
	Volume m_currentMainVolume;

	// sound RAM
	uint16_t m_reverbWorkAreaStartAddress = 0;
	uint16_t m_irqAddress = 0;
	uint16_t m_transferAddressRegister = 0;

	FifoBuffer<uint16_t, SpuFifoSize> m_transferBuffer;

	Control m_control;
	DataTransferControl m_dataTransferControl;
	Status m_status;

	std::array<uint16_t, ReverbRegister::NumRegisters> m_reverbRegisters{};

	uint16_t m_transferAddress = 0;
	Memory<SpuRamSize> m_ram;
};

}