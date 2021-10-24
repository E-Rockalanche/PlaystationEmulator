#include "Spu.h"

#include "DMA.h"

#include <stdx/bit.h>

namespace PSX
{

namespace
{

constexpr uint32_t SpuBase = 0x1F801C00;
constexpr uint32_t SpuVoiceRegistersOffset = 0x1F801C00 - SpuBase;
constexpr uint32_t SpuControlRegistersOffset = 0x1F801D80 - SpuBase;
constexpr uint32_t SpuReverbRegistersOffset = 0x1F801DC0 - SpuBase;
constexpr uint32_t SpuInternalRegistersOffset = 0x1F801E00 - SpuBase;

enum class SpuRegister : uint32_t
{
	VolumeLeft = 0x1F801D80 - SpuBase,
	VolumeRight = VolumeLeft + 2,

	ReverbOutVolumeLeft = 0x1F801D84 - SpuBase,
	ReverbOutVolumeRight = ReverbOutVolumeLeft + 2,

	VoiceKeyOnLow = 0x1F801D88 - SpuBase,
	VoiceKeyOnHigh = VoiceKeyOnLow + 2,
	VoiceKeyOffLow = 0x1F801D8C - SpuBase,
	VoiceKeyOffHigh = VoiceKeyOffLow + 2,
	VoicePitchLow = 0x1F801D90 - SpuBase,
	VoicePitchHigh = VoicePitchLow + 2,
	VoiceNoiseLow = 0x1F801D94 - SpuBase,
	VoiceNoiseHigh = VoiceNoiseLow + 2,
	VoiceReverbLow = 0x1F801D98 - SpuBase,
	VoiceReverbHigh = VoiceReverbLow + 2,
	VoiceStatusLow = 0x1F801D9C - SpuBase,
	VoiceStatusHigh = VoiceStatusLow + 2,

	ReverbWorkAreaStartAddress = 0x1F801DA2 - SpuBase,
	IrqAddress = 0x1F801DA4 - SpuBase,
	DataTransferAddress = 0x1F801DA6 - SpuBase,
	DataTransferFifo = 0x1F801DA8 - SpuBase,
	SpuControl = 0x1F801DAA - SpuBase,
	DataTransferControl = 0x1F801DAC - SpuBase,
	SpuStatus = 0x1F801DAE - SpuBase,

	CDVolumeLeft = 0x1F801DB0 - SpuBase,
	CDVolumeRight = CDVolumeLeft + 2,
	ExternVolumeLeft = 0x1F801DB4 - SpuBase,
	ExternVolumeRight = ExternVolumeLeft + 2,
	CurrentMainVolumeLeft = 0x1F801DB8 - SpuBase,
	CurrentMainVolumeRight = CurrentMainVolumeLeft + 2,
};

} // namespace


uint16_t Spu::Read( uint32_t offset ) noexcept
{
	switch ( static_cast<SpuRegister>( offset ) )
	{
		case SpuRegister::VolumeLeft:	return m_mainVolume.left;
		case SpuRegister::VolumeRight:	return m_mainVolume.right;

		case SpuRegister::ReverbOutVolumeLeft:	return m_reverbOutVolume.left;
		case SpuRegister::ReverbOutVolumeRight:	return m_reverbOutVolume.right;

		case SpuRegister::VoiceKeyOnLow:	return static_cast<uint16_t>( m_voiceFlags.keyOn );
		case SpuRegister::VoiceKeyOnHigh:	return static_cast<uint16_t>( m_voiceFlags.keyOn >> 16 );

		case SpuRegister::VoiceKeyOffLow:	return static_cast<uint16_t>( m_voiceFlags.keyOff );
		case SpuRegister::VoiceKeyOffHigh:	return static_cast<uint16_t>( m_voiceFlags.keyOff >> 16 );

		case SpuRegister::VoicePitchLow:	return static_cast<uint16_t>( m_voiceFlags.pitchModulationEnable );
		case SpuRegister::VoicePitchHigh:	return static_cast<uint16_t>( m_voiceFlags.pitchModulationEnable >> 16 );

		case SpuRegister::VoiceNoiseLow:	return static_cast<uint16_t>( m_voiceFlags.noiseModeEnable );
		case SpuRegister::VoiceNoiseHigh:	return static_cast<uint16_t>( m_voiceFlags.noiseModeEnable >> 16 );

		case SpuRegister::VoiceReverbLow:	return static_cast<uint16_t>( m_voiceFlags.reverbEnable );
		case SpuRegister::VoiceReverbHigh:	return static_cast<uint16_t>( m_voiceFlags.reverbEnable >> 16 );

		case SpuRegister::VoiceStatusLow:	return static_cast<uint16_t>( m_voiceFlags.status );
		case SpuRegister::VoiceStatusHigh:	return static_cast<uint16_t>( m_voiceFlags.status >> 16 );

		case SpuRegister::ReverbWorkAreaStartAddress:	return m_reverbWorkAreaStartAddress;
		case SpuRegister::IrqAddress:					return m_irqAddress;
		case SpuRegister::DataTransferAddress:			return m_dataTransferAddress;
		case SpuRegister::DataTransferFifo:				return 0; // TODO
		case SpuRegister::SpuControl:					return m_control.value;
		case SpuRegister::DataTransferControl:			return m_dataTransferControl;
		case SpuRegister::SpuStatus:					return m_status.value;

		case SpuRegister::CDVolumeLeft:		return m_cdAudioInputVolume.left;
		case SpuRegister::CDVolumeRight:	return m_cdAudioInputVolume.right;

		case SpuRegister::ExternVolumeLeft:		return m_externalAudioInputVolume.left;
		case SpuRegister::ExternVolumeRight:	return m_externalAudioInputVolume.right;

		case SpuRegister::CurrentMainVolumeLeft:	return m_currentMainVolume.left;
		case SpuRegister::CurrentMainVolumeRight:	return m_currentMainVolume.right;

		default:
		{
			if ( offset < SpuControlRegistersOffset )
			{
				// voice register
				// TODO
				return 0xffffu;
			}
			else if ( ( SpuReverbRegistersOffset <= offset ) && ( offset < SpuInternalRegistersOffset ) )
			{
				// reverb register
				// TODO
				return 0xffffu;
			}
			else if ( SpuInternalRegistersOffset <= offset )
			{
				// internal
				// TODO
				return 0xffffu;
			}
			else
			{
				dbLogWarning( "Spu::Read -- invalid offset [%X], address [%X]", offset, offset + SpuBase );
				return 0xffffu;
			}
		}
	}
}

void Spu::Write( uint32_t offset, uint16_t value ) noexcept
{
	static constexpr uint32_t LowMask = 0x0000ffffu;
	static constexpr uint32_t HighMask = 0xffff0000u;

	switch ( static_cast<SpuRegister>( offset ) )
	{
		case SpuRegister::VolumeLeft:	m_mainVolume.left = static_cast<int16_t>( value );	break;
		case SpuRegister::VolumeRight:	m_mainVolume.right = static_cast<int16_t>( value );	break;

		case SpuRegister::ReverbOutVolumeLeft:	m_reverbOutVolume.left = static_cast<int16_t>( value );		break;
		case SpuRegister::ReverbOutVolumeRight:	m_reverbOutVolume.right = static_cast<int16_t>( value );	break;

		case SpuRegister::VoiceKeyOnLow:	stdx::masked_set<uint32_t>( m_voiceFlags.keyOn, LowMask, value );			break;
		case SpuRegister::VoiceKeyOnHigh:	stdx::masked_set<uint32_t>( m_voiceFlags.keyOn, HighMask, value << 16 );	break;

		case SpuRegister::VoiceKeyOffLow:	stdx::masked_set<uint32_t>( m_voiceFlags.keyOff, LowMask, value );			break;
		case SpuRegister::VoiceKeyOffHigh:	stdx::masked_set<uint32_t>( m_voiceFlags.keyOff, HighMask, value << 16 );	break;

		case SpuRegister::VoicePitchLow:	stdx::masked_set<uint32_t>( m_voiceFlags.pitchModulationEnable, LowMask, value );			break;
		case SpuRegister::VoicePitchHigh:	stdx::masked_set<uint32_t>( m_voiceFlags.pitchModulationEnable, HighMask, value << 16 );	break;

		case SpuRegister::VoiceNoiseLow:	stdx::masked_set<uint32_t>( m_voiceFlags.noiseModeEnable, LowMask, value );			break;
		case SpuRegister::VoiceNoiseHigh:	stdx::masked_set<uint32_t>( m_voiceFlags.noiseModeEnable, HighMask, value << 16 );	break;

		case SpuRegister::VoiceReverbLow:	stdx::masked_set<uint32_t>( m_voiceFlags.reverbEnable, LowMask, value );		break;
		case SpuRegister::VoiceReverbHigh:	stdx::masked_set<uint32_t>( m_voiceFlags.reverbEnable, HighMask, value << 16 );	break;

		case SpuRegister::VoiceStatusLow:	stdx::masked_set<uint32_t>( m_voiceFlags.status, LowMask, value );			break;
		case SpuRegister::VoiceStatusHigh:	stdx::masked_set<uint32_t>( m_voiceFlags.status, HighMask, value << 16 );	break;

		case SpuRegister::ReverbWorkAreaStartAddress:	m_reverbWorkAreaStartAddress = value;	break;
		case SpuRegister::IrqAddress:					m_irqAddress = value;					break;

		case SpuRegister::DataTransferAddress:
			// Used for manual write and DMA read/write Spu memory. Writing to this registers stores the written value in 1F801DA6h,
			// and does additional store the value (multiplied by 8) in another internal "current address" register
			// (that internal register does increment during transfers, whilst the 1F801DA6h value DOESN'T increment).
			m_dataTransferAddress = value;
			m_internalCurrentAddress = value * 8;
			break;

		case SpuRegister::DataTransferFifo:
			// Used for manual-write. Not sure if it can be also used for manual read?

			if ( m_dataTransferBuffer.Full() )
			{
				dbLogWarning( "Spu::Write -- data transfer buffer is full" );
				m_dataTransferBuffer.Pop();
			}

			m_dataTransferBuffer.Push( value );
			break;

		case SpuRegister::SpuControl:
			SetSpuControl( value );
			break;

		case SpuRegister::DataTransferControl:
			m_dataTransferControl = value;
			break;

		case SpuRegister::SpuStatus:
			// The SPUSTAT register should be treated read - only( writing is possible in so far that the written value can be read - back for a short moment,
			// however, thereafter the hardware is overwriting that value ).
			break; 

		case SpuRegister::CDVolumeLeft:		m_cdAudioInputVolume.left = static_cast<int16_t>( value );	break;
		case SpuRegister::CDVolumeRight:	m_cdAudioInputVolume.right = static_cast<int16_t>( value );	break;

		case SpuRegister::ExternVolumeLeft:		m_externalAudioInputVolume.left = static_cast<int16_t>( value );	break;
		case SpuRegister::ExternVolumeRight:	m_externalAudioInputVolume.right = static_cast<int16_t>( value );	break;

		case SpuRegister::CurrentMainVolumeLeft:	m_currentMainVolume.left = static_cast<int16_t>( value );	break;
		case SpuRegister::CurrentMainVolumeRight:	m_currentMainVolume.right = static_cast<int16_t>( value );	break;

		default:
		{
			if ( offset < SpuControlRegistersOffset )
			{
				// voice register
				// TODO
			}
			else if ( ( SpuReverbRegistersOffset <= offset ) && ( offset < SpuInternalRegistersOffset ) )
			{
				// reverb register
				// TODO
			}
			else if ( SpuInternalRegistersOffset <= offset )
			{
				// internal
				// TODO
			}
			else
			{
				dbLogWarning( "Spu::Write -- invalid offset [%X], address [%X]", offset, offset + SpuBase );
			}

			break;
		}
	}
}

void Spu::SetSpuControl( uint16_t value ) noexcept
{
	m_control.value = value;

	// SPUSTAT bits 0-5 are the same as SPUCNT, but applied with a delay
	// TODO: delay
	m_status.value = value & Status::ControlMask;
	m_status.dmaReadWriteRequest = stdx::any_of<uint16_t>( value, 1 << 5 ); // seems to be same as SPUCNT.Bit5

	UpdateDmaRequest();
}

void Spu::UpdateDmaRequest() noexcept
{
	m_status.dmaWriteRequest = false;
	m_status.dmaReadRequest = false;
	bool dmaRequest = false;

	switch ( m_control.GetTransfermode() )
	{
		case TransferMode::Stop:
			break;

		case TransferMode::ManualWrite:
			ExecuteManualWrite();
			break;

		case TransferMode::DMAWrite:
			m_status.dmaWriteRequest = true;
			dmaRequest = true;
			break;

		case TransferMode::DMARead:
			m_status.dmaReadRequest = true;
			dmaRequest = true;
			break;
	}

	m_dma->SetRequest( Dma::Channel::Spu, dmaRequest );
}

void Spu::ExecuteManualWrite() noexcept
{
	m_dataTransferBuffer.Clear();
	m_status.dmaBusy = false;
}

}