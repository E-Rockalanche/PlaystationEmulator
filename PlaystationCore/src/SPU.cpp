#include "Spu.h"

#include "EventManager.h"
#include "DMA.h"
#include "InterruptControl.h"

#include <stdx/bit.h>

namespace PSX
{

namespace
{

constexpr uint32_t SpuBaseAddress = 0x1F801C00;

constexpr uint32_t VoiceRegisterOffset = 0;
constexpr uint32_t ControlRegisterOffset = ( 0x1F801D80 - SpuBaseAddress ) / 2;
constexpr uint32_t ReverbRegisterOffset = ( 0x1F801DC0 - SpuBaseAddress ) / 2;
constexpr uint32_t VolumeRegisterOffset = ( 0x1F801E00 - SpuBaseAddress ) / 2;

enum class SpuControlRegister : uint32_t
{
	MainVolumeLeft = ControlRegisterOffset,
	MainVolumeRight,

	ReverbOutVolumeLeft,
	ReverbOutVolumeRight,

	VoiceKeyOnLow,
	VoiceKeyOnHigh,
	VoiceKeyOffLow,
	VoiceKeyOffHigh,
	VoicePitchLow,
	VoicePitchHigh,
	VoiceNoiseLow,
	VoiceNoiseHigh,
	VoiceReverbLow,
	VoiceReverbHigh,
	VoiceStatusLow,
	VoiceStatusHigh,

	Unknown1,

	ReverbWorkAreaStartAddress,
	IrqAddress,
	DataTransferAddress,
	DataTransferFifo,

	SpuControl,
	DataTransferControl,
	SpuStatus,

	CdVolumeLeft,
	CdVolumeRight,
	ExternVolumeLeft,
	ExternVolumeRight,
	CurrentMainVolumeLeft,
	CurrentMainVolumeRight,

	Unknown2,
	Unknown3
};

enum class VoiceRegister
{
	VolumeLeft,
	VolumeRight,
	ADPCMSampleRate,
	ADPCMStartAddress,
	ADSRLow,
	ADSRHigh,
	CurrentADSRVolume,
	ADPCMRepeatAddress
};

constexpr bool Within( uint32_t offset, uint32_t base, uint32_t size ) noexcept
{
	return ( base <= offset && offset < ( base + size ) );
}

std::array<int16_t, 0x200> GaussTable
{
	-0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001,
	-0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001,
	0x0001, 0x0001, 0x0001, 0x0002, 0x0002, 0x0002, 0x0003, 0x0003,
	0x0003, 0x0004, 0x0004, 0x0005, 0x0005, 0x0006, 0x0007, 0x0007,
	0x0008, 0x0009, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E,
	0x000F, 0x0010, 0x0011, 0x0012, 0x0013, 0x0015, 0x0016, 0x0018,
	0x0019, 0x001B, 0x001C, 0x001E, 0x0020, 0x0021, 0x0023, 0x0025,
	0x0027, 0x0029, 0x002C, 0x002E, 0x0030, 0x0033, 0x0035, 0x0038,
	0x003A, 0x003D, 0x0040, 0x0043, 0x0046, 0x0049, 0x004D, 0x0050,
	0x0054, 0x0057, 0x005B, 0x005F, 0x0063, 0x0067, 0x006B, 0x006F,
	0x0074, 0x0078, 0x007D, 0x0082, 0x0087, 0x008C, 0x0091, 0x0096,
	0x009C, 0x00A1, 0x00A7, 0x00AD, 0x00B3, 0x00BA, 0x00C0, 0x00C7,
	0x00CD, 0x00D4, 0x00DB, 0x00E3, 0x00EA, 0x00F2, 0x00FA, 0x0101,
	0x010A, 0x0112, 0x011B, 0x0123, 0x012C, 0x0135, 0x013F, 0x0148,
	0x0152, 0x015C, 0x0166, 0x0171, 0x017B, 0x0186, 0x0191, 0x019C,
	0x01A8, 0x01B4, 0x01C0, 0x01CC, 0x01D9, 0x01E5, 0x01F2, 0x0200,
	0x020D, 0x021B, 0x0229, 0x0237, 0x0246, 0x0255, 0x0264, 0x0273,
	0x0283, 0x0293, 0x02A3, 0x02B4, 0x02C4, 0x02D6, 0x02E7, 0x02F9,
	0x030B, 0x031D, 0x0330, 0x0343, 0x0356, 0x036A, 0x037E, 0x0392,
	0x03A7, 0x03BC, 0x03D1, 0x03E7, 0x03FC, 0x0413, 0x042A, 0x0441,
	0x0458, 0x0470, 0x0488, 0x04A0, 0x04B9, 0x04D2, 0x04EC, 0x0506,
	0x0520, 0x053B, 0x0556, 0x0572, 0x058E, 0x05AA, 0x05C7, 0x05E4,
	0x0601, 0x061F, 0x063E, 0x065C, 0x067C, 0x069B, 0x06BB, 0x06DC,
	0x06FD, 0x071E, 0x0740, 0x0762, 0x0784, 0x07A7, 0x07CB, 0x07EF,
	0x0813, 0x0838, 0x085D, 0x0883, 0x08A9, 0x08D0, 0x08F7, 0x091E,
	0x0946, 0x096F, 0x0998, 0x09C1, 0x09EB, 0x0A16, 0x0A40, 0x0A6C,
	0x0A98, 0x0AC4, 0x0AF1, 0x0B1E, 0x0B4C, 0x0B7A, 0x0BA9, 0x0BD8,
	0x0C07, 0x0C38, 0x0C68, 0x0C99, 0x0CCB, 0x0CFD, 0x0D30, 0x0D63,
	0x0D97, 0x0DCB, 0x0E00, 0x0E35, 0x0E6B, 0x0EA1, 0x0ED7, 0x0F0F,
	0x0F46, 0x0F7F, 0x0FB7, 0x0FF1, 0x102A, 0x1065, 0x109F, 0x10DB,
	0x1116, 0x1153, 0x118F, 0x11CD, 0x120B, 0x1249, 0x1288, 0x12C7,
	0x1307, 0x1347, 0x1388, 0x13C9, 0x140B, 0x144D, 0x1490, 0x14D4,
	0x1517, 0x155C, 0x15A0, 0x15E6, 0x162C, 0x1672, 0x16B9, 0x1700,
	0x1747, 0x1790, 0x17D8, 0x1821, 0x186B, 0x18B5, 0x1900, 0x194B,
	0x1996, 0x19E2, 0x1A2E, 0x1A7B, 0x1AC8, 0x1B16, 0x1B64, 0x1BB3,
	0x1C02, 0x1C51, 0x1CA1, 0x1CF1, 0x1D42, 0x1D93, 0x1DE5, 0x1E37,
	0x1E89, 0x1EDC, 0x1F2F, 0x1F82, 0x1FD6, 0x202A, 0x207F, 0x20D4,
	0x2129, 0x217F, 0x21D5, 0x222C, 0x2282, 0x22DA, 0x2331, 0x2389,
	0x23E1, 0x2439, 0x2492, 0x24EB, 0x2545, 0x259E, 0x25F8, 0x2653,
	0x26AD, 0x2708, 0x2763, 0x27BE, 0x281A, 0x2876, 0x28D2, 0x292E,
	0x298B, 0x29E7, 0x2A44, 0x2AA1, 0x2AFF, 0x2B5C, 0x2BBA, 0x2C18,
	0x2C76, 0x2CD4, 0x2D33, 0x2D91, 0x2DF0, 0x2E4F, 0x2EAE, 0x2F0D,
	0x2F6C, 0x2FCC, 0x302B, 0x308B, 0x30EA, 0x314A, 0x31AA, 0x3209,
	0x3269, 0x32C9, 0x3329, 0x3389, 0x33E9, 0x3449, 0x34A9, 0x3509,
	0x3569, 0x35C9, 0x3629, 0x3689, 0x36E8, 0x3748, 0x37A8, 0x3807,
	0x3867, 0x38C6, 0x3926, 0x3985, 0x39E4, 0x3A43, 0x3AA2, 0x3B00,
	0x3B5F, 0x3BBD, 0x3C1B, 0x3C79, 0x3CD7, 0x3D35, 0x3D92, 0x3DEF,
	0x3E4C, 0x3EA9, 0x3F05, 0x3F62, 0x3FBD, 0x4019, 0x4074, 0x40D0,
	0x412A, 0x4185, 0x41DF, 0x4239, 0x4292, 0x42EB, 0x4344, 0x439C,
	0x43F4, 0x444C, 0x44A3, 0x44FA, 0x4550, 0x45A6, 0x45FC, 0x4651,
	0x46A6, 0x46FA, 0x474E, 0x47A1, 0x47F4, 0x4846, 0x4898, 0x48E9,
	0x493A, 0x498A, 0x49D9, 0x4A29, 0x4A77, 0x4AC5, 0x4B13, 0x4B5F,
	0x4BAC, 0x4BF7, 0x4C42, 0x4C8D, 0x4CD7, 0x4D20, 0x4D68, 0x4DB0,
	0x4DF7, 0x4E3E, 0x4E84, 0x4EC9, 0x4F0E, 0x4F52, 0x4F95, 0x4FD7,
	0x5019, 0x505A, 0x509A, 0x50DA, 0x5118, 0x5156, 0x5194, 0x51D0,
	0x520C, 0x5247, 0x5281, 0x52BA, 0x52F3, 0x532A, 0x5361, 0x5397,
	0x53CC, 0x5401, 0x5434, 0x5467, 0x5499, 0x54CA, 0x54FA, 0x5529,
	0x5558, 0x5585, 0x55B2, 0x55DE, 0x5609, 0x5632, 0x565B, 0x5684,
	0x56AB, 0x56D1, 0x56F6, 0x571B, 0x573E, 0x5761, 0x5782, 0x57A3,
	0x57C3, 0x57E2, 0x57FF, 0x581C, 0x5838, 0x5853, 0x586D, 0x5886,
	0x589E, 0x58B5, 0x58CB, 0x58E0, 0x58F4, 0x5907, 0x5919, 0x592A,
	0x593A, 0x5949, 0x5958, 0x5965, 0x5971, 0x597C, 0x5986, 0x598F,
	0x5997, 0x599E, 0x59A4, 0x59A9, 0x59AD, 0x59B0, 0x59B2, 0x59B3,
};

} // namespace

Spu::Spu( InterruptControl& interruptControl, EventManager& eventManager )
	: m_interruptControl{ interruptControl }
{
	m_transferEvent = eventManager.CreateEvent( "SPU Transfer Event", [this]( cycles_t cycles ) { UpdateTransferEvent( cycles ); } );

	m_generateSoundEvent = eventManager.CreateEvent( "SPU Generate Sound Event", [this]( cycles_t cycles ) { GenerateSamples( cycles ); } );
}

void Spu::Reset()
{
	m_transferEvent->Cancel();

	m_voices = {};

	m_mainVolume = {};
	m_reverbOutVolume = {};

	m_voiceFlags = {};

	m_reverbWorkAreaStartAddress = 0;
	m_irqAddress = 0;
	m_transferAddressRegister = 0;
	m_transferBufferRegister = 0;

	m_control.value = 0;
	m_dataTransferControl.value = 0;
	m_status.value = 0;

	m_cdAudioInputVolume = {};
	m_externalAudioInputVolume = {};
	m_currentMainVolume = {};

	m_reverb.registers.fill( 0 );

	m_voiceVolumes = {};

	m_unknownRegisters.fill( 0 );

	m_transferBuffer.Reset();

	m_transferAddress = 0;

	m_ram.Fill( 0 );
}

uint16_t Spu::Read( uint32_t offset ) noexcept
{
	switch ( static_cast<SpuControlRegister>( offset ) )
	{
		case SpuControlRegister::MainVolumeLeft:	return m_mainVolume[ 0 ].value;
		case SpuControlRegister::MainVolumeRight:	return m_mainVolume[ 1 ].value;

		case SpuControlRegister::ReverbOutVolumeLeft:	return m_reverbOutVolume[ 0 ].value;
		case SpuControlRegister::ReverbOutVolumeRight:	return m_reverbOutVolume[ 1 ].value;

		case SpuControlRegister::VoiceKeyOnLow:		return static_cast<uint16_t>( m_voiceFlags.keyOn );
		case SpuControlRegister::VoiceKeyOnHigh:	return static_cast<uint16_t>( m_voiceFlags.keyOn >> 16 );

		case SpuControlRegister::VoiceKeyOffLow:	return static_cast<uint16_t>( m_voiceFlags.keyOff );
		case SpuControlRegister::VoiceKeyOffHigh:	return static_cast<uint16_t>( m_voiceFlags.keyOff >> 16 );

		case SpuControlRegister::VoicePitchLow:		return static_cast<uint16_t>( m_voiceFlags.pitchModulationEnable );
		case SpuControlRegister::VoicePitchHigh:	return static_cast<uint16_t>( m_voiceFlags.pitchModulationEnable >> 16 );

		case SpuControlRegister::VoiceNoiseLow:		return static_cast<uint16_t>( m_voiceFlags.noiseModeEnable );
		case SpuControlRegister::VoiceNoiseHigh:	return static_cast<uint16_t>( m_voiceFlags.noiseModeEnable >> 16 );

		case SpuControlRegister::VoiceReverbLow:	return static_cast<uint16_t>( m_voiceFlags.reverbEnable );
		case SpuControlRegister::VoiceReverbHigh:	return static_cast<uint16_t>( m_voiceFlags.reverbEnable >> 16 );

		case SpuControlRegister::VoiceStatusLow:	return static_cast<uint16_t>( m_voiceFlags.status );
		case SpuControlRegister::VoiceStatusHigh:	return static_cast<uint16_t>( m_voiceFlags.status >> 16 );

		case SpuControlRegister::ReverbWorkAreaStartAddress:	return m_reverbWorkAreaStartAddress;
		case SpuControlRegister::IrqAddress:					return m_irqAddress;
		case SpuControlRegister::DataTransferAddress:			return m_transferAddressRegister;
		case SpuControlRegister::DataTransferFifo:				return 0xffff;
		case SpuControlRegister::SpuControl:					return m_control.value;
		case SpuControlRegister::DataTransferControl:			return m_dataTransferControl.value;

		case SpuControlRegister::SpuStatus:
			GeneratePendingSamples();
			return m_status.value;

		case SpuControlRegister::CdVolumeLeft:	return m_cdAudioInputVolume[ 0 ];
		case SpuControlRegister::CdVolumeRight:	return m_cdAudioInputVolume[ 1 ];

		case SpuControlRegister::ExternVolumeLeft:	return m_externalAudioInputVolume[ 0 ];
		case SpuControlRegister::ExternVolumeRight:	return m_externalAudioInputVolume[ 1 ];

		case SpuControlRegister::CurrentMainVolumeLeft:
			GeneratePendingSamples();
			return m_currentMainVolume[ 0 ];

		case SpuControlRegister::CurrentMainVolumeRight:
			GeneratePendingSamples();
			return m_currentMainVolume[ 1 ];

		case SpuControlRegister::Unknown1:
		case SpuControlRegister::Unknown2:
		case SpuControlRegister::Unknown3:	return 0xffff;

		default:
		{
			if ( Within( offset, 0, VoiceCount * VoiceRegisterCount ) )
			{
				// voices

				return ReadVoiceRegister( offset );
			}
			else if ( Within( offset, ReverbRegisterOffset, ReverbRegisterCount ) )
			{
				// reverb

				return m_reverb.registers[ offset - ReverbRegisterOffset ];
			}
			else if ( Within( offset, VolumeRegisterOffset, VoiceCount * VoiceVolumeRegisterCount ) )
			{
				// volumes

				GeneratePendingSamples();
				const uint32_t volumeIndex = ( offset - VolumeRegisterOffset ) / 2;
				const uint32_t volumeRegister = ( offset - VolumeRegisterOffset ) % 2;
				return m_voiceVolumes[ volumeIndex ][ volumeRegister ].value;
			}
			else
			{
				dbLogWarning( "Spu::Read -- unknown register [%u]", offset );
				return 0xffff;
			}
		}
	}
}

void Spu::Write( uint32_t offset, uint16_t value ) noexcept
{
	static constexpr uint32_t LowMask = 0x0000ffffu;
	static constexpr uint32_t HighMask = 0xffff0000u;

	switch ( static_cast<SpuControlRegister>( offset ) )
	{
		case SpuControlRegister::MainVolumeLeft:
			GeneratePendingSamples();
			m_mainVolume[ 0 ].value = static_cast<int16_t>( value );
			break;

		case SpuControlRegister::MainVolumeRight:
			GeneratePendingSamples();
			m_mainVolume[ 1 ].value = static_cast<int16_t>( value );
			break;

		case SpuControlRegister::ReverbOutVolumeLeft:
			GeneratePendingSamples();
			m_reverbOutVolume[ 0 ].value = static_cast<int16_t>( value );
			break;

		case SpuControlRegister::ReverbOutVolumeRight:
			GeneratePendingSamples();
			m_reverbOutVolume[ 1 ].value = static_cast<int16_t>( value );
			break;

		case SpuControlRegister::VoiceKeyOnLow:
			GeneratePendingSamples();
			stdx::masked_set<uint32_t>( m_voiceFlags.keyOn, LowMask, value );		
			break;

		case SpuControlRegister::VoiceKeyOnHigh:
			GeneratePendingSamples();
			stdx::masked_set<uint32_t>( m_voiceFlags.keyOn, HighMask, value << 16 );
			break;

		case SpuControlRegister::VoiceKeyOffLow:
			GeneratePendingSamples();
			stdx::masked_set<uint32_t>( m_voiceFlags.keyOff, LowMask, value );		
			break;

		case SpuControlRegister::VoiceKeyOffHigh:
			GeneratePendingSamples();
			stdx::masked_set<uint32_t>( m_voiceFlags.keyOff, HighMask, value << 16 );
			break;

		case SpuControlRegister::VoicePitchLow:
			GeneratePendingSamples();
			stdx::masked_set<uint32_t>( m_voiceFlags.pitchModulationEnable, LowMask, value );		
			break;

		case SpuControlRegister::VoicePitchHigh:
			GeneratePendingSamples();
			stdx::masked_set<uint32_t>( m_voiceFlags.pitchModulationEnable, HighMask, value << 16 );
			break;

		case SpuControlRegister::VoiceNoiseLow:
			GeneratePendingSamples();
			stdx::masked_set<uint32_t>( m_voiceFlags.noiseModeEnable, LowMask, value );		
			break;

		case SpuControlRegister::VoiceNoiseHigh:
			GeneratePendingSamples();
			stdx::masked_set<uint32_t>( m_voiceFlags.noiseModeEnable, HighMask, value << 16 );
			break;

		case SpuControlRegister::VoiceReverbLow:
			GeneratePendingSamples();
			stdx::masked_set<uint32_t>( m_voiceFlags.reverbEnable, LowMask, value );	
			break;

		case SpuControlRegister::VoiceReverbHigh:
			GeneratePendingSamples();
			stdx::masked_set<uint32_t>( m_voiceFlags.reverbEnable, HighMask, value << 16 );
			break;

		case SpuControlRegister::ReverbWorkAreaStartAddress:
			GeneratePendingSamples();
			m_reverbWorkAreaStartAddress = value;
			break;

		case SpuControlRegister::IrqAddress:
			GeneratePendingSamples();
			m_irqAddress = value;
			TryTriggerInterrupt();
			break;

		case SpuControlRegister::DataTransferAddress:
		{
			// Used for manual write and DMA read/write Spu memory. Writing to this registers stores the written value in 1F801DA6h,
			// and does additional store the value (multiplied by 8) in another internal "current address" register
			// (that internal register does increment during transfers, whilst the 1F801DA6h value DOESN'T increment).
			m_transferAddressRegister = value;
			m_transferAddress = ( value * 8 ) & SpuRamAddressMask;
			TryTriggerInterrupt();
			break;
		}

		case SpuControlRegister::DataTransferFifo:
		{
			// Used for manual-write. Not sure if it can be also used for manual read?

			if ( m_transferBuffer.Full() )
			{
				dbLogWarning( "Spu::Write -- data transfer buffer is full" );
				break;
			}

			m_transferBuffer.Push( value );
			ScheduleTransferEvent();
			break;
		}

		case SpuControlRegister::SpuControl:
			SetSpuControl( value );
			break;

		case SpuControlRegister::DataTransferControl:
			m_dataTransferControl.value = value;
			break;

		case SpuControlRegister::CdVolumeLeft:
			GeneratePendingSamples();
			m_cdAudioInputVolume[ 0 ] = static_cast<int16_t>( value );
			break;

		case SpuControlRegister::CdVolumeRight:
			GeneratePendingSamples();
			m_cdAudioInputVolume[ 1 ] = static_cast<int16_t>( value );
			break;

		case SpuControlRegister::ExternVolumeLeft:
			GeneratePendingSamples();
			m_externalAudioInputVolume[ 0 ] = static_cast<int16_t>( value );
			break;

		case SpuControlRegister::ExternVolumeRight:
			GeneratePendingSamples();
			m_externalAudioInputVolume[ 1 ] = static_cast<int16_t>( value );
			break;

		default:
		{
			if ( Within( offset, 0, VoiceCount * VoiceRegisterCount ) )
			{
				// voices

				WriteVoiceRegister( offset, value );
			}
			else if ( Within( offset, ReverbRegisterOffset, ReverbRegisterCount ) )
			{
				// reverb

				GeneratePendingSamples();
				m_reverb.registers[ offset - ReverbRegisterOffset ] = value;
			}
			else
			{
				dbLogWarning( "Spu::Write -- unknown register [%X -> %u]", value, offset );
			}
			break;
		}
	}
}

uint16_t Spu::ReadVoiceRegister( uint32_t offset ) noexcept
{
	const uint32_t voiceIndex = offset / 8;
	const uint32_t registerIndex = offset % 8;

	const auto& voice = m_voices[ voiceIndex ];

	// update if reading volume and voice is on or pending key on
	if ( ( static_cast<VoiceRegister>( registerIndex ) == VoiceRegister::CurrentADSRVolume ) &&
		( voice.IsOn() || ( m_voiceFlags.keyOn & ( 1 << voiceIndex ) ) ) )
	{
		GeneratePendingSamples();
	}

	return voice.registers.values[ registerIndex ];
}

void Spu::WriteVoiceRegister( uint32_t offset, uint16_t value ) noexcept
{
	const uint32_t voiceIndex = offset / 8;
	const uint32_t registerIndex = offset % 8;

	auto& voice = m_voices[ voiceIndex ];

	// update if voice is on or pending on
	if ( voice.IsOn() || ( m_voiceFlags.keyOn & ( 1 << voiceIndex ) ) )
		GeneratePendingSamples();

	switch ( static_cast<VoiceRegister>( registerIndex ) )
	{
		case VoiceRegister::VolumeLeft:
			voice.registers.volumeLeft.value = value;
			voice.volumeLeft.Reset( voice.registers.volumeLeft );
			break;

		case VoiceRegister::VolumeRight:
			voice.registers.volumeRight.value = value;
			voice.volumeRight.Reset( voice.registers.volumeRight );
			break;

		case VoiceRegister::ADPCMSampleRate:
			voice.registers.adpcmSampleRate = value;
			break;

		case VoiceRegister::ADPCMStartAddress:
			voice.registers.adpcmStartAddress = value;
			break;

		case VoiceRegister::ADSRLow:
		{
			voice.registers.adsr.valueLow = value;
			if ( voice.IsOn() )
				voice.UpdateADSREnvelope();

			break;
		}

		case VoiceRegister::ADSRHigh:
		{
			voice.registers.adsr.valueHigh = value;
			if ( voice.IsOn() )
				voice.UpdateADSREnvelope();

			break;
		}

		case VoiceRegister::CurrentADSRVolume:
			voice.registers.currentADSRVolume = value;
			break;

		case VoiceRegister::ADPCMRepeatAddress:
		{
			// There is a short window of time here between the voice being keyed on and the first block finishing decoding
			// where setting the repeat address will *NOT* ignore the block/loop start flag. Games sensitive to this are:
			//  - The Misadventures of Tron Bonne
			//  - Re-Loaded - The Hardcore Sequel
			//  - Valkyrie Profile

			const bool ignoreLoopAddress = voice.IsOn() && !voice.firstBlock;
			voice.registers.adpcmRepeatAddress = value;
			voice.ignoreLoopAddress |= ignoreLoopAddress;
			break;
		}
	}
}

void Spu::DmaWrite( const uint32_t* dataIn, uint32_t count ) noexcept
{
	const uint32_t halfwords = count * 2;
	const uint32_t available = std::min( halfwords, m_transferBuffer.Capacity() );

	if ( available < halfwords )
		dbLogWarning( "Spu::DmaWrite -- fifo buffer overflow" );

	m_transferBuffer.Push( reinterpret_cast<const uint16_t*>( dataIn ), available );

	UpdateDmaRequest();
	ScheduleTransferEvent();
}

void Spu::DmaRead( uint32_t* dataOut, uint32_t count ) noexcept
{
	const uint32_t halfwords = count * 2;
	uint16_t* dest = reinterpret_cast<uint16_t*>( dataOut );

	const uint32_t available = std::min( halfwords, m_transferBuffer.Size() );

	for ( uint32_t i = 0; i < available; ++i )
		dest[ i ] = m_transferBuffer.Pop();

	if ( available < halfwords )
	{
		dbLogWarning( "Spu::DmaRead -- fifo buffer overflow" );
		std::fill_n( dest + available, halfwords - available, uint16_t( 0xffff ) );
	}

	UpdateDmaRequest();
	ScheduleTransferEvent();
}

void Spu::SetSpuControl( uint16_t value ) noexcept
{
	GeneratePendingSamples();

	Control newControl{ value };

	// duckstation finishes partial transfer and clears fifo here
	if ( newControl.soundRamTransferMode != m_control.soundRamTransferMode &&
		newControl.GetTransfermode() == TransferMode::Stop )
	{
		m_transferBuffer.Clear();
	}

	if ( !newControl.enable && m_control.enable )
	{
		for ( auto& voice : m_voices )
			voice.ForceOff();
	}

	m_control.value = value;

	// SPUSTAT bits 0-5 are the same as SPUCNT, but applied with a delay (delay not required)
	m_status.value = value & Status::ControlMask;

	if ( !newControl.irqEnable )
		m_status.irq = false;
	else
		TryTriggerInterrupt();

	UpdateDmaRequest();
	ScheduleTransferEvent();
}

inline void Spu::TriggerInterrupt() noexcept
{
	dbExpects( CanTriggerInterrupt() );
	m_status.irq = true;
	m_interruptControl.SetInterrupt( Interrupt::Spu );
}

void Spu::UpdateDmaRequest() noexcept
{
	bool request = false;

	switch ( m_control.GetTransfermode() )
	{
		case TransferMode::Stop:
		case TransferMode::ManualWrite:
			m_status.dmaRequest = false;
			m_status.dmaReadRequest = false;
			m_status.dmaWriteRequest = false;
			break;

		case TransferMode::DMAWrite:
			request = m_transferBuffer.Empty();
			m_status.dmaWriteRequest = request;
			m_status.dmaRequest = request;
			m_status.dmaReadRequest = false;
			break;

		case TransferMode::DMARead:
			request = !m_transferBuffer.Empty();
			m_status.dmaReadRequest = request;
			m_status.dmaRequest = request;
			m_status.dmaWriteRequest = false;
			break;
	}

	m_dma->SetRequest( Dma::Channel::Spu, request );
}

void Spu::ScheduleTransferEvent() noexcept
{
	auto schedule = [this]( uint32_t halfwords )
	{
		if ( halfwords == 0 )
			m_transferEvent->Cancel();
		else
			m_transferEvent->Schedule( static_cast<cycles_t>( halfwords * TransferCyclesPerHalfword ) );
	};

	switch ( m_control.GetTransfermode() )
	{
		case TransferMode::Stop:
			m_transferEvent->Cancel();
			break;

		case TransferMode::DMARead:
			schedule( m_transferBuffer.Capacity() );
			break;

		case TransferMode::DMAWrite:
		case TransferMode::ManualWrite:
			schedule( m_transferBuffer.Size() );
			break;
	}

	m_status.transferBusy = m_transferEvent->IsActive();
}

void Spu::UpdateTransferEvent( cycles_t cycles ) noexcept
{
	dbExpects( m_control.GetTransfermode() != TransferMode::Stop );

	if ( m_control.GetTransfermode() == TransferMode::DMARead )
	{
		dbAssert( static_cast<cycles_t>( m_transferBuffer.Capacity() * TransferCyclesPerHalfword ) == cycles );

		while ( !m_transferBuffer.Full() )
		{
			m_transferBuffer.Push( m_ram.Read<uint16_t>( m_transferAddress ) );
			m_transferAddress = ( m_transferAddress + 2 ) & SpuRamAddressMask;
			TryTriggerInterrupt();
		}
	}
	else
	{
		dbAssert( static_cast<cycles_t>( m_transferBuffer.Size() * TransferCyclesPerHalfword ) == cycles );

		while ( !m_transferBuffer.Empty() )
		{
			m_ram.Write( m_transferAddress, m_transferBuffer.Pop() );
			m_transferAddress = ( m_transferAddress + 2 ) & SpuRamAddressMask;
			TryTriggerInterrupt();
		}
	}

	// wait for a DMA before transfering more data
	UpdateDmaRequest();
	ScheduleTransferEvent();
}

} // namespace PSX