#include "Spu.h"

#include "EventManager.h"
#include "DMA.h"
#include "InterruptControl.h"

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

Spu::Spu( InterruptControl& interruptControl, EventManager& eventManager )
	: m_interruptControl{ interruptControl }
{
	m_transferEvent = eventManager.CreateEvent( "SPU Transfer Event", [this]( cycles_t cycles ) { UpdateTransferEvent( cycles ); } );
}

void Spu::Reset()
{
	m_transferEvent->Cancel();

	m_voices.fill( Voice{} );
	m_currentVolumes.fill( Volume{} );
	m_voiceFlags = VoiceFlags{};

	m_mainVolume = Volume{};
	m_reverbOutVolume = Volume{};
	m_cdAudioInputVolume = Volume{};
	m_externalAudioInputVolume = Volume{};
	m_currentMainVolume = Volume{};

	m_reverbWorkAreaStartAddress = 0;
	m_irqAddress = 0;
	m_transferAddressRegister = 0;

	m_transferBuffer.Reset();

	m_control.value = 0;
	m_dataTransferControl.value = 0;
	m_status.value = 0;

	m_reverbRegisters.fill( 0 );

	m_transferAddress = 0;
	m_ram.Fill( 0 );
}

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
		case SpuRegister::DataTransferAddress:			return m_transferAddressRegister;
		case SpuRegister::DataTransferFifo:				return 0; // TODO
		case SpuRegister::SpuControl:					return m_control.value;
		case SpuRegister::DataTransferControl:			return m_dataTransferControl.value;
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
				const size_t voiceIndex = ( offset - SpuVoiceRegistersOffset ) / sizeof( Voice );
				const size_t registerIndex = ( offset - SpuVoiceRegistersOffset ) % ( sizeof( Voice ) / 2 );
				return m_voices[ voiceIndex ].registers[ registerIndex ];
			}
			else if ( ( SpuReverbRegistersOffset <= offset ) && ( offset < SpuInternalRegistersOffset ) )
			{
				return m_reverbRegisters[ ( offset - SpuReverbRegistersOffset ) / 2 ];
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
			m_transferAddressRegister = value;
			m_transferAddress = ( value * 8 ) & SpuRamAddressMask;
			break;

		case SpuRegister::DataTransferFifo:
			// Used for manual-write. Not sure if it can be also used for manual read?

			if ( m_transferBuffer.Full() )
			{
				dbLogWarning( "Spu::Write -- data transfer buffer is full" );
				m_transferBuffer.Pop();
			}

			m_transferBuffer.Push( value );
			break;

		case SpuRegister::SpuControl:
			SetSpuControl( value );
			break;

		case SpuRegister::DataTransferControl:
			m_dataTransferControl.value = value;
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
				const size_t voiceIndex = ( offset - SpuVoiceRegistersOffset ) / sizeof( Voice );
				const size_t registerIndex = ( offset - SpuVoiceRegistersOffset ) % ( sizeof( Voice ) / 2 );
				m_voices[ voiceIndex ].registers[ registerIndex ] = value;
			}
			else if ( ( SpuReverbRegistersOffset <= offset ) && ( offset < SpuInternalRegistersOffset ) )
			{
				m_reverbRegisters[ ( offset - SpuReverbRegistersOffset / 2 ) ] = value;
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