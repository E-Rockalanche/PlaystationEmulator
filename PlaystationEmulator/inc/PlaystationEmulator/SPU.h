#pragma once

#include "FifoBuffer.h"

#include <array>
#include <cstdint>

namespace PSX
{

class Spu
{
public:
	uint16_t Read( uint32_t offset ) noexcept;
	void Write( uint32_t offset, uint16_t value ) noexcept;

private:
	void SetSpuControl( uint16_t value ) noexcept;

	void TransferDataToRam() noexcept;

private:
	static constexpr uint32_t VoiceCount = 24;

	struct Volume
	{
		int16_t left = 0;
		int16_t right = 0;
	};

	union Voice
	{
		Voice() noexcept : registers{} {}
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
		Control() noexcept : value{ 0 } {}
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
		uint16_t value;
	};
	static_assert( sizeof( Control ) == 2 );

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

	union Status
	{
		Status() noexcept : value{ 0 } {}
		struct
		{
			uint16_t currentMode : 6; // (same as SPUCNT.Bit5-0, but, applied a bit delayed)
			uint16_t irq : 1;
			uint16_t dmaReadWriteRequest : 1;
			uint16_t dmaWriteRequest : 1;
			uint16_t dmaReadRequest : 1;
			uint16_t dmaBusy : 1;
			uint16_t writingToCaptureBufferHalf : 1;
			uint16_t : 4;
		};
		uint16_t value;
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

	// meaningful registers range from 0x1F801C00 to 0x1F801E5f

	std::array<Voice, VoiceCount> m_voices;
	std::array<Volume, VoiceCount> m_currentVolumes;
	VoiceFlags m_voiceFlags;

	Volume m_mainVolume;
	Volume m_reverbOutVolume;
	Volume m_cdAudioInputVolume; // for nomrla CD-DA and compressed XA-ADPCM
	Volume m_externalAudioInputVolume;
	Volume m_currentMainVolume;

	// sound RAM
	uint16_t m_reverbWorkAreaStartAddress = 0;
	uint16_t m_irqAddress = 0;
	uint16_t m_dataTransferAddress = 0;

	FifoBuffer<uint16_t, 32> m_dataTransferBuffer;

	Control m_control;

	/*
	The Transfer Type selects how data is forwarded from Fifo to SPU RAM:
		__Transfer Type___Halfwords in Fifo________Halfwords written to SPU RAM__
		0,1,6,7  Fill     A,B,C,D,E,F,G,H,...,X    X,X,X,X,X,X,X,X,...
		2        Normal   A,B,C,D,E,F,G,H,...,X    A,B,C,D,E,F,G,H,...
		3        Rep2     A,B,C,D,E,F,G,H,...,X    A,A,C,C,E,E,G,G,...
		4        Rep4     A,B,C,D,E,F,G,H,...,X    A,A,A,A,E,E,E,E,...
		5        Rep8     A,B,C,D,E,F,G,H,...,X    H,H,H,H,H,H,H,H,...
	*/
	uint16_t m_dataTransferControl = 0; // 1-3: Sound RAM Data Transfer Type (should be 2)

	Status m_status;

	std::array<uint16_t, ReverbRegister::NumRegisters> m_reverbRegisters{};

	uint16_t m_internalCurrentAddress = 0;
};

}