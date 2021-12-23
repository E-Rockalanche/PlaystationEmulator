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
	Spu( CDRomDrive& cdromDrive, InterruptControl& interruptControl, EventManager& eventManager, AudioQueue& audioQueue );

	void Reset();

	void SetDma( Dma& dma ) noexcept { m_dma = &dma; }

	uint16_t Read( uint32_t offset ) noexcept;

	void Write( uint32_t offset, uint16_t value ) noexcept;

	void DmaWrite( const uint32_t* dataIn, uint32_t count ) noexcept;
	void DmaRead( uint32_t* dataOut, uint32_t count ) noexcept;

	void EndFrame() noexcept;

private:
	static constexpr uint32_t VoiceCount = 24;
	static constexpr uint32_t VoiceRegisterCount = 8;
	static constexpr uint32_t ControlRegisterCount = 32;
	static constexpr uint32_t ReverbRegisterCount = 32;
	static constexpr uint32_t VoiceVolumeRegisterCount = 2;
	static constexpr uint32_t SpuFifoSize = 32;
	static constexpr uint32_t SpuRamSize = 0x80000;
	static constexpr uint32_t SpuRamAddressMask = SpuRamSize - 1;
	static constexpr uint32_t SampleRate = 44100;
	static constexpr uint32_t SamplesPerADPCMBlock = 28;
	static constexpr uint32_t OldSamplesForInterpolation = 3;
	static constexpr uint32_t CaptureBufferSize = 0x400;

	static constexpr cycles_t TransferCyclesPerHalfword = 16;
	static constexpr cycles_t CyclesPerAudioFrame = CpuCyclesPerSecond / SampleRate;

	static constexpr int16_t EnvelopeMinVolume = 0;
	static constexpr int16_t EnvelopeMaxVolume = std::numeric_limits<int16_t>::max();

	static_assert( CyclesPerAudioFrame* SampleRate == CpuCyclesPerSecond );

	enum class TransferMode
	{
		Stop,
		ManualWrite,
		DMAWrite,
		DMARead
	};

	union ADPCMHeader
	{
		uint8_t GetShift() const noexcept
		{
			const uint8_t s = shift;
			return ( s <= 12 ) ? s : 9;
		}

		uint8_t GetFilter() const noexcept
		{
			const uint8_t f = filter;
			return ( f <= 4 ) ? f : 4;
		}

		struct
		{
			uint8_t shift : 4;
			uint8_t filter : 3;
			uint8_t : 1;
		};
		uint8_t value = 0;
	};
	static_assert( sizeof( ADPCMHeader ) == 1 );

	union ADPCMFlags
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
	static_assert( sizeof( ADPCMFlags ) == 1 );

	struct ADPCMBlock
	{
		ADPCMHeader header;
		ADPCMFlags flags;
		std::array<uint8_t, SamplesPerADPCMBlock / 2> data;
	};
	static_assert( sizeof( ADPCMBlock ) == 16 );

	union VoiceADSR
	{
		struct
		{
			uint16_t sustainLevel : 4;

			// decayStep always -8
			uint16_t decayShift : 4;
			// decayDirection always decreasing
			// decayMode always exponential

			uint16_t attackRate : 7; // step and shift
			// attackDirection always increasing
			uint16_t attackMode : 1;

			// releaseStep always -8
			uint16_t releaseShift : 5;
			// releaseDirection always decreasing
			uint16_t releaseMode : 1;

			uint16_t sustainRate : 7; // step and shift
			uint16_t : 1;
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

	union VolumeRegister
	{
		struct
		{
			int16_t fixedVolume : 15; // divided by 2
			int16_t : 1;
		};
		struct
		{
			uint16_t sweepRate : 7; // step and shift
			uint16_t : 5;
			uint16_t sweepPhase : 1;
			uint16_t sweepDirection : 1;
			uint16_t sweepMode : 1;

			uint16_t sweepVolume : 1; // 0=fixed volume, 1=sweep volume
		};
		uint16_t value = 0;
	};
	static_assert( sizeof( VolumeRegister ) == 2 );

	union VoiceRegisters
	{
		VoiceRegisters() : values{} {}

		struct
		{
			VolumeRegister volumeLeft;
			VolumeRegister volumeRight;
			uint16_t adpcmSampleRate; // (VxPitch)
			uint16_t adpcmStartAddress; // x8
			VoiceADSR adsr;
			int16_t currentADSRVolume;
			uint16_t adpcmRepeatAddress; // x8
		};
		std::array<uint16_t, VoiceRegisterCount> values;
	};
	static_assert( sizeof( VoiceRegisters ) == VoiceRegisterCount * 2 );

	struct VoiceFlags
	{
		uint32_t keyOn = 0;					// 0=No change, 1=Start Attack/Decay/Sustain
		uint32_t keyOff = 0;				// 0=No change, 1=Start Release
		uint32_t pitchModulationEnable = 0;
		uint32_t noiseModeEnable = 0;
		uint32_t reverbEnable = 0;
		uint32_t endx = 0;					// 0=Newly Keyed On, 1=Reached LOOP-END
	};

	union Control
	{
		Control() noexcept = default;
		Control( uint16_t v ) noexcept : value{ v } {}

		struct
		{
			uint16_t cdAudioEnable : 1;
			uint16_t externalAudioEnable : 1;
			uint16_t cdAudioReverb : 1;
			uint16_t externalAudioReverb : 1;
			uint16_t soundRamTransferMode : 2;
			uint16_t irqEnable : 1;
			uint16_t reverbMasterEnable : 1;

			uint16_t noiseFrequencyRate : 6; // step and shift
			uint16_t unmute : 1;
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

			// duckstation has the read and write request bits backwards?? Maybe games don't care
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
			uint16_t sameSideReflectionAddress1[ 2 ];
			uint16_t combAddress1[ 2 ];
			uint16_t combAddress2[ 2 ];
			uint16_t sameSideReflectionAddress2[ 2 ];
			uint16_t differentSideReflectionAddress1[ 2 ];
			uint16_t combAddress3[ 2 ];
			uint16_t combAddress4[ 2 ];
			uint16_t differentSideReflectionAddress2[ 2 ];
			uint16_t apfAddress1[ 2 ];
			uint16_t apfAddress2[ 2 ];
			int16_t inputVolume[ 2 ];
		};
		std::array<uint16_t, ReverbRegisterCount> registers{};
	};
	static_assert( sizeof( ReverbRegisters ) == ReverbRegisterCount * 2 );

	union VoiceCounter
	{
		struct
		{
			uint32_t : 4;
			uint32_t interpolationIndex : 8;
			uint32_t sampleIndex : 5;
			uint32_t : 15;
		};
		uint32_t value = 0;
	};
	static_assert( sizeof( VoiceCounter ) == 4 );

	struct VolumeEnvelope
	{
		int32_t counter = 0;
		uint8_t rate = 0;
		bool decreasing = false;
		bool exponential = false;

		void Reset( uint8_t rate_, bool decreasing_, bool exponential_ ) noexcept;

		int16_t Tick( int16_t currentLevel ) noexcept;
	};

	struct VolumeSweep
	{
		VolumeEnvelope envelope;
		bool envelopeActive = false;
		int16_t currentLevel = 0;

		void Reset( VolumeRegister reg ) noexcept;

		void Tick() noexcept;
	};

	enum class ADSRPhase : uint8_t
	{
		Off,
		Attack,
		Decay,
		Sustain,
		Release
	};

	static constexpr ADSRPhase GetNextADSRPhase( ADSRPhase phase ) noexcept
	{
		constexpr std::array<ADSRPhase, 5> NextPhase
		{
			ADSRPhase::Off,		// off     -> off
			ADSRPhase::Decay,	// attack  -> decay
			ADSRPhase::Sustain,	// decay   -> sustain
			ADSRPhase::Sustain,	// sustain -> sustain (until key off)
			ADSRPhase::Off		// release -> off
		};
		return NextPhase[ static_cast<size_t>( phase ) ];
	}

	struct Voice
	{
		VoiceRegisters registers;

		uint16_t currentAddress = 0;
		VoiceCounter counter;
		ADPCMFlags currentBlockFlags;
		bool firstBlock = false;
		std::array<int16_t, SamplesPerADPCMBlock + OldSamplesForInterpolation> currentBlockSamples{};
		std::array<int16_t, 2> adpcmLastSamples{};
		int32_t lastVolume = 0;

		std::array<VolumeSweep, 2> volume;

		VolumeEnvelope adsrEnvelope;
		ADSRPhase adsrPhase;
		int16_t adsrTarget = 0;
		bool hasSamples = false;
		bool ignoreLoopAddress = false;

		bool IsOn() const noexcept { return adsrPhase != ADSRPhase::Off; }

		void KeyOn() noexcept;
		void KeyOff() noexcept;
		void ForceOff() noexcept;

		void DecodeBlock( const ADPCMBlock& block ) noexcept;
		int32_t Interpolate() const noexcept;

		void UpdateADSREnvelope() noexcept;

		void TickADSR() noexcept;
	};

private:
	uint16_t ReadVoiceRegister( uint32_t offset ) noexcept;
	void WriteVoiceRegister( uint32_t offset, uint16_t value ) noexcept;

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

	void CheckForLateInterrupt() noexcept;

	void ScheduleGenerateSamplesEvent() noexcept;
	void GeneratePendingSamples() noexcept;
	void GenerateSamples( cycles_t cycles ) noexcept;

	std::pair<int32_t, int32_t> SampleVoice( uint32_t voiceIndex ) noexcept;

	ADPCMBlock ReadADPCMBlock( uint16_t address ) noexcept;

	void UpdateNoise() noexcept;

	void WriteToCaptureBuffer( uint32_t index, int16_t sample ) noexcept;

	void KeyVoices() noexcept;

	int16_t GetCurrentNoiseLevel() const noexcept { return static_cast<int16_t>( m_noiseLevel ); }

	uint32_t ReverbMemoryAddress( uint32_t address ) const noexcept;

	int16_t ReverbRead( uint32_t address, int32_t offset = 0 ) noexcept;

	void ReverbWrite( uint32_t address, int16_t data ) noexcept;

	std::pair<int32_t, int32_t> ProcessReverb( int16_t inLeft, int16_t inRight ) noexcept;

private:
	CDRomDrive& m_cdromDrive;
	InterruptControl& m_interruptControl;
	AudioQueue& m_audioQueue;
	Dma* m_dma = nullptr;

	EventHandle m_generateSamplesEvent;
	EventHandle m_transferEvent;

	std::array<Voice, VoiceCount> m_voices;

	std::array<VolumeRegister, 2> m_mainVolumeRegisters;
	std::array<VolumeSweep, 2> m_mainVolume;
	std::array<int16_t, 2> m_reverbOutVolume = {};

	VoiceFlags m_voiceFlags;

	uint16_t m_irqAddress = 0;

	uint16_t m_transferAddressRegister = 0;
	uint32_t m_transferAddress = 0;

	Control m_control;
	DataTransferControl m_dataTransferControl;
	Status m_status;

	std::array<int16_t, 2> m_cdAudioInputVolume = {}; // for normal CD-DA and compressed XA-ADPCM
	std::array<int16_t, 2> m_externalAudioInputVolume = {};
	std::array<int16_t, 2> m_currentMainVolume = {};

	uint16_t m_reverbBaseAddressRegister = 0;
	uint32_t m_reverbBaseAddress = 0;
	uint32_t m_reverbCurrentAddress = 0;
	int32_t m_reverbResampleBufferPosition = 0;
	ReverbRegisters m_reverb;
	std::array<std::array<int16_t, 128>, 2> m_reverbDownsampleBuffer = {};
	std::array<std::array<int16_t, 64>, 2> m_reverbUpsampleBuffer = {};
	
	FifoBuffer<uint16_t, SpuFifoSize> m_transferBuffer;

	uint32_t m_captureBufferPosition = 0;

	uint32_t m_noiseCount = 0;
	uint32_t m_noiseLevel = 0;

	cycles_t m_pendingCarryCycles = 0;

	uint32_t m_generatedFrames = 0;

	Memory<SpuRamSize> m_ram;
};

}