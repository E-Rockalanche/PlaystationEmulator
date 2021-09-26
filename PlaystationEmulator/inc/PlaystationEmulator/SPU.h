#pragma once

#include <array>
#include <cstdint>

namespace PSX
{

class SPU
{
public:
	uint16_t Read( uint32_t index ) noexcept
	{

	}

	void Write( uint32_t index, uint16_t value ) noexcept;

private:
	static constexpr uint32_t VoiceCount = 24;

	class Voice
	{
		enum Register
		{
			VolumeLeft,
			VolumeRight,
			ADPCMSampleRate,
			ADPCMStartAddress,
			ADSR1,
			ADSR2,
			CurrentADSRVolume,
			ADPCMRepeatAddress
		};


		std::array<uint16_t, 8> m_registers;
	};

	struct Volume
	{
		int16_t left = 0;
		int16_t right = 0;
	};

	struct VoiceFlags
	{
		uint32_t keyOn = 0;
		uint32_t keyOff = 0;
		uint32_t status = 0;
		uint32_t noiseModeEnable = 0;
	};

	std::array<Voice, VoiceCount> m_voices;
	std::array<Volume, VoiceCount> m_currentVolumes;

	Volume m_mainVolume;
	Volume m_cdAudioInputVolume; // for nomrla CD-DA and compressed XA-ADPCM
	Volume m_externalAudioInputVolume;
	Volume m_currentMainVolume;

	VoiceFlags m_voiceFlags;

	uint16_t m_control = 0;
	uint16_t m_status = 0;
};

}