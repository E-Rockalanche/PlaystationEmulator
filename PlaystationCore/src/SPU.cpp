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

constexpr uint32_t VoiceRegisterCount = 24 * 8;
constexpr uint32_t ReverbRegisterCount = 32;
constexpr uint32_t VolumeRegisterCount = 24 * 2;

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
static_assert( static_cast<uint32_t>( SpuControlRegister::Unknown3 ) == ControlRegisterOffset + 31 );

constexpr bool Within( uint32_t offset, uint32_t base, uint32_t size ) noexcept
{
	return ( base <= offset && offset < ( base + size ) );
}

} // namespace

Spu::Spu( InterruptControl& interruptControl, EventManager& eventManager )
	: m_interruptControl{ interruptControl }
{
	m_transferEvent = eventManager.CreateEvent( "SPU Transfer Event", [this]( cycles_t cycles ) { UpdateTransferEvent( cycles ); } );
}

void Spu::Reset()
{
	m_transferEvent->Cancel();

	m_voices.fill( Voice{} );

	m_mainVolume = Volume{};
	m_reverbOutVolume = Volume{};

	m_voiceFlags = VoiceFlags{};

	m_reverbWorkAreaStartAddress = 0;
	m_irqAddress = 0;
	m_transferAddressRegister = 0;
	m_transferBufferRegister = 0;

	m_control.value = 0;
	m_dataTransferControl.value = 0;
	m_status.value = 0;

	m_cdAudioInputVolume = Volume{}; // for normal CD-DA and compressed XA-ADPCM
	m_externalAudioInputVolume = Volume{};
	m_currentMainVolume = Volume{};

	m_reverb.registers.fill( 0 );

	m_voiceVolumes.fill( Volume{} );

	m_unknownRegisters.fill( 0 );

	m_transferBuffer.Reset();

	m_transferAddress = 0;

	m_ram.Fill( 0 );
}

uint16_t Spu::Read( uint32_t offset ) noexcept
{
	switch ( static_cast<SpuControlRegister>( offset ) )
	{
		case SpuControlRegister::MainVolumeLeft:	return m_mainVolume.left;
		case SpuControlRegister::MainVolumeRight:	return m_mainVolume.right;

		case SpuControlRegister::ReverbOutVolumeLeft:	return m_reverbOutVolume.left;
		case SpuControlRegister::ReverbOutVolumeRight:	return m_reverbOutVolume.right;

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
		case SpuControlRegister::SpuStatus:						return m_status.value;

		case SpuControlRegister::CdVolumeLeft:	return m_cdAudioInputVolume.left;
		case SpuControlRegister::CdVolumeRight:	return m_cdAudioInputVolume.right;

		case SpuControlRegister::ExternVolumeLeft:	return m_externalAudioInputVolume.left;
		case SpuControlRegister::ExternVolumeRight:	return m_externalAudioInputVolume.right;

		case SpuControlRegister::CurrentMainVolumeLeft:		return m_currentMainVolume.left;
		case SpuControlRegister::CurrentMainVolumeRight:	return m_currentMainVolume.right;

		case SpuControlRegister::Unknown1:
		case SpuControlRegister::Unknown2:
		case SpuControlRegister::Unknown3:	return 0xffff;

		default:
		{
			if ( Within( offset, 0, VoiceRegisterCount ) )
			{
				// voices

				const uint32_t voiceIndex = offset / 8;
				const uint32_t voiceRegister = offset % 8;

				// TODO
				return 0;
			}
			else if ( Within( offset, ReverbRegisterOffset, ReverbRegisterCount ) )
			{
				// reverb

				return m_reverb.registers[ offset - ReverbRegisterOffset ];
			}
			else if ( Within( offset, VolumeRegisterOffset, VolumeRegisterCount ) )
			{
				// volumes

				const uint32_t volumeIndex = ( offset - VolumeRegisterOffset ) / 2;
				const uint32_t volumeRegister = ( offset - VolumeRegisterOffset ) % 2;

				// TODO
				return 0;
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
		case SpuControlRegister::MainVolumeLeft:	m_mainVolume.left = static_cast<int16_t>( value );	break;
		case SpuControlRegister::MainVolumeRight:	m_mainVolume.right = static_cast<int16_t>( value );	break;

		case SpuControlRegister::ReverbOutVolumeLeft:	m_reverbOutVolume.left = static_cast<int16_t>( value );		break;
		case SpuControlRegister::ReverbOutVolumeRight:	m_reverbOutVolume.right = static_cast<int16_t>( value );	break;

		case SpuControlRegister::VoiceKeyOnLow:		stdx::masked_set<uint32_t>( m_voiceFlags.keyOn, LowMask, value );			break;
		case SpuControlRegister::VoiceKeyOnHigh:	stdx::masked_set<uint32_t>( m_voiceFlags.keyOn, HighMask, value << 16 );	break;

		case SpuControlRegister::VoiceKeyOffLow:	stdx::masked_set<uint32_t>( m_voiceFlags.keyOff, LowMask, value );			break;
		case SpuControlRegister::VoiceKeyOffHigh:	stdx::masked_set<uint32_t>( m_voiceFlags.keyOff, HighMask, value << 16 );	break;

		case SpuControlRegister::VoicePitchLow:		stdx::masked_set<uint32_t>( m_voiceFlags.pitchModulationEnable, LowMask, value );			break;
		case SpuControlRegister::VoicePitchHigh:	stdx::masked_set<uint32_t>( m_voiceFlags.pitchModulationEnable, HighMask, value << 16 );	break;

		case SpuControlRegister::VoiceNoiseLow:		stdx::masked_set<uint32_t>( m_voiceFlags.noiseModeEnable, LowMask, value );			break;
		case SpuControlRegister::VoiceNoiseHigh:	stdx::masked_set<uint32_t>( m_voiceFlags.noiseModeEnable, HighMask, value << 16 );	break;

		case SpuControlRegister::VoiceReverbLow:	stdx::masked_set<uint32_t>( m_voiceFlags.reverbEnable, LowMask, value );		break;
		case SpuControlRegister::VoiceReverbHigh:	stdx::masked_set<uint32_t>( m_voiceFlags.reverbEnable, HighMask, value << 16 );	break;

		case SpuControlRegister::VoiceStatusLow:	stdx::masked_set<uint32_t>( m_voiceFlags.status, LowMask, value );			break;
		case SpuControlRegister::VoiceStatusHigh:	stdx::masked_set<uint32_t>( m_voiceFlags.status, HighMask, value << 16 );	break;

		case SpuControlRegister::ReverbWorkAreaStartAddress:	m_reverbWorkAreaStartAddress = value;	break;
		case SpuControlRegister::IrqAddress:					m_irqAddress = value;					break;

		case SpuControlRegister::DataTransferAddress:
		{
			// Used for manual write and DMA read/write Spu memory. Writing to this registers stores the written value in 1F801DA6h,
			// and does additional store the value (multiplied by 8) in another internal "current address" register
			// (that internal register does increment during transfers, whilst the 1F801DA6h value DOESN'T increment).
			m_transferAddressRegister = value;
			m_transferAddress = ( value * 8 ) & SpuRamAddressMask;
			break;
		}

		case SpuControlRegister::DataTransferFifo:
		{
			// Used for manual-write. Not sure if it can be also used for manual read?

			if ( m_transferBuffer.Full() )
			{
				dbLogWarning( "Spu::Write -- data transfer buffer is full" );
				m_transferBuffer.Pop();
			}

			m_transferBuffer.Push( value );
			break;
		}

		case SpuControlRegister::SpuControl:
			SetSpuControl( value );
			break;

		case SpuControlRegister::DataTransferControl:
			m_dataTransferControl.value = value;
			break;

		case SpuControlRegister::SpuStatus:
			// The SPUSTAT register should be treated read - only( writing is possible in so far that the written value can be read - back for a short moment,
			// however, thereafter the hardware is overwriting that value ).
			break; 

		case SpuControlRegister::CdVolumeLeft:	m_cdAudioInputVolume.left = static_cast<int16_t>( value );	break;
		case SpuControlRegister::CdVolumeRight:	m_cdAudioInputVolume.right = static_cast<int16_t>( value );	break;

		case SpuControlRegister::ExternVolumeLeft:	m_externalAudioInputVolume.left = static_cast<int16_t>( value );	break;
		case SpuControlRegister::ExternVolumeRight:	m_externalAudioInputVolume.right = static_cast<int16_t>( value );	break;

		case SpuControlRegister::CurrentMainVolumeLeft:		m_currentMainVolume.left = static_cast<int16_t>( value );	break;
		case SpuControlRegister::CurrentMainVolumeRight:	m_currentMainVolume.right = static_cast<int16_t>( value );	break;

		default:
		{
			if ( Within( offset, 0, VoiceRegisterCount ) )
			{
				// voices

				const uint32_t voiceIndex = offset / 8;
				const uint32_t voiceRegister = offset % 8;

				// TODO
			}
			else if ( Within( offset, ReverbRegisterOffset, ReverbRegisterCount ) )
			{
				// reverb

				m_reverb.registers[ offset - ReverbRegisterOffset ] = value;
			}
			else if ( Within( offset, VolumeRegisterOffset, VolumeRegisterCount ) )
			{
				// volumes

				const uint32_t volumeIndex = ( offset - VolumeRegisterOffset ) / 2;
				const uint32_t volumeRegister = ( offset - VolumeRegisterOffset ) % 2;

				// TODO
			}
			else
			{
				dbLogWarning( "Spu::Write -- unknown register [%X -> %u]", value, offset );
			}
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
	m_control.value = value;

	// SPUSTAT bits 0-5 are the same as SPUCNT, but applied with a delay (delay not required)
	m_status.value = value & Status::ControlMask;

	UpdateDmaRequest();
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

	m_status.dmaBusy = m_transferEvent->IsActive();
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
			TryTriggerInterrupt( m_transferAddress );
		}
	}
	else
	{
		dbAssert( static_cast<cycles_t>( m_transferBuffer.Size() * TransferCyclesPerHalfword ) == cycles );

		while ( !m_transferBuffer.Empty() )
		{
			m_ram.Write( m_transferAddress, m_transferBuffer.Pop() );
			m_transferAddress = ( m_transferAddress + 2 ) & SpuRamAddressMask;
			TryTriggerInterrupt( m_transferAddress );
		}
	}

	// wait for a DMA before transfering more data
	UpdateDmaRequest();
}

}