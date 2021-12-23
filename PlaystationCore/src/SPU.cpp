#include "Spu.h"

#include "AudioQueue.h"
#include "CDRomDrive.h"
#include "DMA.h"
#include "EventManager.h"
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

inline constexpr bool Within( uint32_t offset, uint32_t base, uint32_t size ) noexcept
{
	return ( base <= offset && offset < ( base + size ) );
}

inline constexpr int32_t ApplyVolume( int32_t sample, int16_t volume ) noexcept
{
	return static_cast<int32_t>( ( sample * volume ) >> 15 );
}

inline constexpr int16_t SaturateSample( int32_t sample ) noexcept
{
	constexpr int32_t Min = std::numeric_limits<int16_t>::min();
	constexpr int32_t Max = std::numeric_limits<int16_t>::max();
	return static_cast<int16_t>( ( sample < Min ) ? Min : ( sample > Max ) ? Max : sample );
}

constexpr std::array<int32_t, 5> AdpcmPosTable = { { 0, 60, 115, 98, 122 } };
constexpr std::array<int32_t, 5> AdpcmNegTable = { { 0, 0, -52, -55, -60 } };

constexpr std::array<int16_t, 0x200> GaussTable
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

// ADSR table code from Duckstation
struct ADSRTableEntry
{
	int32_t ticks;
	int32_t step;
};
static constexpr uint32_t ADSRTableEntryCount = 128;
static constexpr uint32_t ADSRDirectionCount = 2;

using ADSRTableEntries = std::array<std::array<ADSRTableEntry, ADSRTableEntryCount>, ADSRDirectionCount>;

constexpr ADSRTableEntries ComputeADSRTableEntries() noexcept
{
	ADSRTableEntries entries = {};
	for ( uint32_t direction = 0; direction < ADSRDirectionCount; direction++ )
	{
		for ( uint32_t rate = 0; rate < ADSRTableEntryCount; rate++ )
		{
			if ( rate < 48 )
			{
				entries[ direction ][ rate ].ticks = 1;
				if ( direction != 0 )
					entries[ direction ][ rate ].step =
					static_cast<int32_t>( static_cast<uint32_t>( -8 + static_cast<int32_t>( rate & 3 ) ) << ( 11 - ( rate >> 2 ) ) );
				else
					entries[ direction ][ rate ].step = ( 7 - static_cast<int32_t>( rate & 3 ) ) << ( 11 - ( rate >> 2 ) );
			}
			else
			{
				entries[ direction ][ rate ].ticks = 1 << ( static_cast<int32_t>( rate >> 2 ) - 11 );
				if ( direction != 0 )
					entries[ direction ][ rate ].step = ( -8 + static_cast<int32_t>( rate & 3 ) );
				else
					entries[ direction ][ rate ].step = ( 7 - static_cast<int32_t>( rate & 3 ) );
			}
		}
	}

	return entries;
}

constexpr ADSRTableEntries ADSRTable = ComputeADSRTableEntries();

///////////////////////////////////////////////////////////////////////
// Reverb algorithm taken from Duckstation (taken from Mednafen-PSX) //
///////////////////////////////////////////////////////////////////////

// Zeroes optimized out; middle removed too(it's 16384)
constexpr std::array<int16_t, 20> ReverbResampleCoefficients
{
  -1, 2, -10, 35, -103, 266, -616, 1332, -2960, 10246, 10246, -2960, 1332, -616, 266, -103, 35, -10, 2, -1,
};

STDX_forceinline int32_t Reverb4422( const int16_t* src ) noexcept
{
	int32_t output = 0; // 32-bits is adequate(it won't overflow)

	for ( uint32_t i = 0; i < 20; i++ )
		output += ReverbResampleCoefficients[ i ] * src[ i * 2 ];

	// Middle non-zero
	output += 0x4000 * src[ 19 ];
	output >>= 15;
	return std::clamp<int32_t>( output, -32768, 32767 );
}

template <bool Phase>
STDX_forceinline int32_t Reverb2244( const int16_t* src ) noexcept
{
	if constexpr ( Phase )
	{
		return src[ 9 ]; // Middle non-zero
	}
	else
	{
		int32_t output = 0; // 32-bits is adequate(it won't overflow)

		for ( uint32_t i = 0; i < 20; i++ )
			output += ReverbResampleCoefficients[ i ] * src[ i ];

		output >>= 14;
		output = std::clamp<int32_t>( output, -32768, 32767 );

		return output;
	}
}

STDX_forceinline int16_t ReverbSat( int32_t value ) noexcept
{
	return static_cast<int16_t>( std::clamp<int32_t>( value, -0x8000, 0x7FFF ) );
}

STDX_forceinline int16_t ReverbNeg( int16_t sample ) noexcept
{
	return ( sample == -32768 ) ? 0x7FFF : -sample;
}

STDX_forceinline int32_t IIASM( const int16_t iirAlpha, const int16_t sample ) noexcept
{
	if ( iirAlpha == -32768 )
	{
		if ( sample == -32768 )
			return 0;
		else
			return sample * -65536;
	}
	else
	{
		return sample * ( 32768 - iirAlpha );
	}
}

} // namespace

void Spu::VolumeEnvelope::Reset( uint8_t rate_, bool decreasing_, bool exponential_ ) noexcept
{
	rate = rate_;
	decreasing = decreasing_;
	exponential = exponential_;
	counter = ADSRTable[ decreasing_ ][ rate_ ].ticks;
}

int16_t Spu::VolumeEnvelope::Tick( int16_t currentLevel ) noexcept
{
	counter--;
	if ( counter > 0 )
		return currentLevel;

	const auto& entry = ADSRTable[ decreasing ][ rate ];
	int32_t curStep = entry.step;
	counter = entry.ticks;

	if ( exponential )
	{
		if ( decreasing )
		{
			curStep = ( curStep * currentLevel ) >> 15;
		}
		else
		{
			if ( currentLevel >= 0x6000 )
			{
				if ( rate < 40 )
				{
					curStep >>= 2;
				}
				else if ( rate >= 44 )
				{
					counter >>= 2;
				}
				else
				{
					curStep >>= 1;
					counter >>= 1;
				}
			}
		}
	}

	return static_cast<int16_t>( std::clamp<int32_t>( currentLevel + curStep, EnvelopeMinVolume, EnvelopeMaxVolume ) );
}

void Spu::VolumeSweep::Reset( VolumeRegister reg ) noexcept
{
	if ( reg.sweepVolume )
	{
		envelope.Reset( reg.sweepRate, reg.sweepDirection, reg.sweepMode );
		envelopeActive = true;
	}
	else
	{
		currentLevel = reg.fixedVolume * 2;
		envelopeActive = false;
	}
}

void Spu::VolumeSweep::Tick() noexcept
{
	if ( envelopeActive )
	{
		currentLevel = envelope.Tick( currentLevel );
		envelopeActive = envelope.decreasing ? ( currentLevel > EnvelopeMinVolume ) : ( currentLevel < EnvelopeMaxVolume );
	}
}

void Spu::Voice::KeyOn() noexcept
{
	currentAddress = registers.adpcmStartAddress & ~1;
	counter.value = 0;
	registers.currentADSRVolume = 0;
	adpcmLastSamples.fill( 0 );

	// previous samples should be 0 to prevent audio clicks
	for ( uint32_t i = 0; i < OldSamplesForInterpolation; ++i )
		currentBlockSamples[ SamplesPerADPCMBlock + i ] = 0;

	hasSamples = false;
	firstBlock = true;
	ignoreLoopAddress = false;
	adsrPhase = ADSRPhase::Attack;

	UpdateADSREnvelope();
}

void Spu::Voice::KeyOff() noexcept
{
	switch ( adsrPhase )
	{
		case ADSRPhase::Off:
		case ADSRPhase::Release:
			// already off or releasing. No change
			break;

		default:
			adsrPhase = ADSRPhase::Release;
			UpdateADSREnvelope();
			break;
	}
}

void Spu::Voice::ForceOff() noexcept
{
	adsrPhase = ADSRPhase::Off;
	registers.currentADSRVolume = 0;
}

void Spu::Voice::UpdateADSREnvelope() noexcept
{
	switch ( adsrPhase )
	{
		case ADSRPhase::Off:
			adsrTarget = 0;
			adsrEnvelope.Reset( 0, false, false );
			break;

		case ADSRPhase::Attack:
			adsrTarget = EnvelopeMaxVolume;
			adsrEnvelope.Reset( registers.adsr.attackRate, false, registers.adsr.attackMode ); // always increasing
			break;

		case ADSRPhase::Decay:
			adsrTarget = static_cast<int16_t>( std::min<int32_t>( ( registers.adsr.sustainLevel + 1 ) * 0x800, EnvelopeMaxVolume ) );
			adsrEnvelope.Reset( static_cast<uint8_t>( registers.adsr.decayShift << 2 ), true, true ); // always decreasing, always exponential
			break;

		case ADSRPhase::Sustain:
			adsrTarget = 0;
			adsrEnvelope.Reset( registers.adsr.sustainRate, registers.adsr.sustainDirection, registers.adsr.sustainMode );
			break;

		case ADSRPhase::Release:
			adsrTarget = 0;
			adsrEnvelope.Reset( static_cast<uint8_t>( registers.adsr.releaseShift << 2 ), true, registers.adsr.releaseMode ); // always decreasing
			break;
	}
}

void Spu::Voice::TickADSR() noexcept
{
	registers.currentADSRVolume = adsrEnvelope.Tick( registers.currentADSRVolume );

	if ( adsrPhase != ADSRPhase::Sustain )
	{
		const bool hitTarget = adsrEnvelope.decreasing ? ( registers.currentADSRVolume <= adsrTarget ) : ( registers.currentADSRVolume >= adsrTarget );
		if ( hitTarget )
		{
			adsrPhase = GetNextADSRPhase( adsrPhase );
			UpdateADSREnvelope();
		}
	}
}

void Spu::Voice::DecodeBlock( const ADPCMBlock& block ) noexcept
{
	// shift latest 3 samples to beginning for interpolation
	for ( uint32_t i = 0; i < OldSamplesForInterpolation; ++i )
		currentBlockSamples[ i ] = currentBlockSamples[ SamplesPerADPCMBlock + i ];

	const uint8_t shift = block.header.GetShift();
	const uint8_t filterIndex = block.header.GetFilter();
	const int32_t filterPos = AdpcmPosTable[ filterIndex ];
	const int32_t filterNeg = AdpcmNegTable[ filterIndex ];

	std::array<int16_t, 2> lastSamples = adpcmLastSamples;

	for ( uint32_t i = 0; i < SamplesPerADPCMBlock; ++i )
	{
		const uint16_t nibble = ( block.data[ i / 2 ] >> ( ( i % 2 ) * 4 ) ) & 0xf;

		int32_t rawSample = static_cast<int32_t>( static_cast<int16_t>( nibble << 12 ) >> shift );
		rawSample += ( lastSamples[ 0 ] * filterPos ) >> 6;
		rawSample += ( lastSamples[ 1 ] * filterNeg ) >> 6;

		const int16_t sample = SaturateSample( rawSample );

		lastSamples[ 1 ] = lastSamples[ 0 ];
		lastSamples[ 0 ] = sample;
		currentBlockSamples[ OldSamplesForInterpolation + i ] = sample;
	}

	adpcmLastSamples = lastSamples;
	currentBlockFlags.value = block.flags.value;
	hasSamples = true;
}

int32_t Spu::Voice::Interpolate() const noexcept
{
	const uint8_t i = counter.interpolationIndex;
	const uint32_t s = counter.sampleIndex + OldSamplesForInterpolation;

	int32_t output = static_cast<int32_t>( GaussTable[ 0x0ff - i ] ) * static_cast<int32_t>( currentBlockSamples[ s - 3 ] );
	output += static_cast<int32_t>( GaussTable[ 0x1ff - i ] ) * static_cast<int32_t>( currentBlockSamples[ s - 2 ] );
	output += static_cast<int32_t>( GaussTable[ 0x100 + i ] ) * static_cast<int32_t>( currentBlockSamples[ s - 1 ] );
	output += static_cast<int32_t>( GaussTable[ 0x000 + i ] ) * static_cast<int32_t>( currentBlockSamples[ s - 0 ] );

	return output >> 15;
}

Spu::Spu( CDRomDrive& cdromDrive, InterruptControl& interruptControl, EventManager& eventManager, AudioQueue& audioQueue )
	: m_cdromDrive{ cdromDrive }
	, m_interruptControl{ interruptControl }
	, m_audioQueue{ audioQueue }
{
	m_transferEvent = eventManager.CreateEvent( "SPU Transfer Event", [this]( cycles_t cycles ) { UpdateTransferEvent( cycles ); } );

	m_generateSamplesEvent = eventManager.CreateEvent( "SPU Generate Sound Event", [this]( cycles_t cycles ) { GenerateSamples( cycles ); } );
}

void Spu::Reset()
{
	m_transferEvent->Cancel();
	m_generateSamplesEvent->Cancel();

	m_voices = {};

	m_mainVolumeRegisters = {};
	m_mainVolume = {};
	m_reverbOutVolume = {};

	m_voiceFlags = {};

	m_irqAddress = 0;

	m_transferAddressRegister = 0;
	m_transferAddress = 0;

	m_control.value = 0;
	m_dataTransferControl.value = 0;
	m_status.value = 0;

	m_cdAudioInputVolume = {};
	m_externalAudioInputVolume = {};
	m_currentMainVolume = {};

	m_reverbBaseAddressRegister = 0;
	m_reverbBaseAddress = 0;
	m_reverbCurrentAddress = 0;
	m_reverbResampleBufferPosition = 0;
	m_reverb.registers = {};
	m_reverbDownsampleBuffer = {};
	m_reverbUpsampleBuffer = {};

	m_transferBuffer.Reset();

	m_captureBufferPosition = 0;

	m_noiseCount = 0;
	m_noiseLevel = 1;

	m_pendingCarryCycles = 0;

	m_generatedFrames = 0;

	m_ram.Fill( 0 );

	ScheduleGenerateSamplesEvent();
}

void Spu::EndFrame() noexcept
{
	GeneratePendingSamples();

	dbLogDebug( "Spu::EndFrame -- Generated frames: %u, total in queue: %u", m_generatedFrames, static_cast<uint32_t>( m_audioQueue.Size() / 2 ) );
	m_generatedFrames = 0;
}

uint16_t Spu::Read( uint32_t offset ) noexcept
{
	switch ( static_cast<SpuControlRegister>( offset ) )
	{
		case SpuControlRegister::MainVolumeLeft:	return m_mainVolumeRegisters[ 0 ].value;
		case SpuControlRegister::MainVolumeRight:	return m_mainVolumeRegisters[ 1 ].value;

		case SpuControlRegister::ReverbOutVolumeLeft:	return static_cast<uint16_t>( m_reverbOutVolume[ 0 ] );
		case SpuControlRegister::ReverbOutVolumeRight:	return static_cast<uint16_t>( m_reverbOutVolume[ 1 ] );

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

		case SpuControlRegister::VoiceStatusLow:	return static_cast<uint16_t>( m_voiceFlags.endx );
		case SpuControlRegister::VoiceStatusHigh:	return static_cast<uint16_t>( m_voiceFlags.endx >> 16 );

		case SpuControlRegister::ReverbWorkAreaStartAddress:	return m_reverbBaseAddressRegister;
		case SpuControlRegister::IrqAddress:					return m_irqAddress;
		case SpuControlRegister::DataTransferAddress:			return m_transferAddressRegister;
		case SpuControlRegister::DataTransferFifo:				return 0xffff; // can't read from fifo
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
				return static_cast<uint16_t>( m_voices[ volumeIndex ].volume[ volumeRegister ].currentLevel );
			}
			else
			{
				dbBreakMessage( "Spu::Read -- unknown register [%u]", offset );
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
			m_mainVolumeRegisters[ 0 ].value = static_cast<int16_t>( value );
			m_mainVolume[ 0 ].Reset( m_mainVolumeRegisters[ 0 ] );
			break;

		case SpuControlRegister::MainVolumeRight:
			GeneratePendingSamples();
			m_mainVolumeRegisters[ 1 ].value = static_cast<int16_t>( value );
			m_mainVolume[ 1 ].Reset( m_mainVolumeRegisters[ 1 ] );
			break;

		case SpuControlRegister::ReverbOutVolumeLeft:
			GeneratePendingSamples();
			m_reverbOutVolume[ 0 ] = static_cast<int16_t>( value );
			break;

		case SpuControlRegister::ReverbOutVolumeRight:
			GeneratePendingSamples();
			m_reverbOutVolume[ 1 ] = static_cast<int16_t>( value );
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

		case SpuControlRegister::VoiceStatusLow:
		case SpuControlRegister::VoiceStatusHigh:
			// read only
			dbLogWarning( "Spu::Write -- writing to voice status [%X]", value );
			break;

		case SpuControlRegister::ReverbWorkAreaStartAddress:
			GeneratePendingSamples();
			m_reverbBaseAddressRegister = value;
			m_reverbCurrentAddress = m_reverbBaseAddress = ( value * 4 ) & 0x3ffff;
			break;

		case SpuControlRegister::IrqAddress:
			m_transferEvent->UpdateEarly();
			GeneratePendingSamples();
			m_irqAddress = value;
			CheckForLateInterrupt();
			break;

		case SpuControlRegister::DataTransferAddress:
		{
			// Used for manual write and DMA read/write Spu memory. Writing to this registers stores the written value in 1F801DA6h,
			// and does additional store the value (multiplied by 8) in another internal "current address" register
			// (that internal register does increment during transfers, whilst the 1F801DA6h value DOESN'T increment).
			m_transferEvent->UpdateEarly();
			m_transferAddressRegister = value;
			m_transferAddress = ( value * 8 ) & SpuRamAddressMask;
			TryTriggerInterrupt( m_transferAddress );
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

		case SpuControlRegister::SpuStatus:
			// read only
			dbLogWarning( "Spu::Write -- writing to SPUSTAT" );
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
			// external volume isn't used. Don't need to sync
			m_externalAudioInputVolume[ 0 ] = static_cast<int16_t>( value );
			break;

		case SpuControlRegister::ExternVolumeRight:
			// external volume isn't used. Don't need to sync
			m_externalAudioInputVolume[ 1 ] = static_cast<int16_t>( value );
			break;

		default:
		{
			if ( Within( offset, 0, VoiceCount * VoiceRegisterCount ) )
			{
				WriteVoiceRegister( offset, value );
			}
			else if ( Within( offset, ReverbRegisterOffset, ReverbRegisterCount ) )
			{
				GeneratePendingSamples();
				const uint32_t index = offset - ReverbRegisterOffset;
				m_reverb.registers[ index ] = value;
			}
			else
			{
				dbBreakMessage( "Spu::Write -- unknown register [%X -> %u]", value, offset );
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

	// generate samples if reading volume or repeat address
	if ( ( registerIndex >= (uint32_t)VoiceRegister::CurrentADSRVolume ) &&
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

	// generate samples before updating register
	if ( voice.IsOn() || ( m_voiceFlags.keyOn & ( 1 << voiceIndex ) ) )
		GeneratePendingSamples();

	switch ( static_cast<VoiceRegister>( registerIndex ) )
	{
		case VoiceRegister::VolumeLeft:
			voice.registers.volumeLeft.value = value;
			voice.volume[ 0 ].Reset( voice.registers.volumeLeft );
			break;

		case VoiceRegister::VolumeRight:
			voice.registers.volumeRight.value = value;
			voice.volume[ 1 ].Reset( voice.registers.volumeRight );
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
			voice.registers.currentADSRVolume = static_cast<int16_t>( value );
			break;

		case VoiceRegister::ADPCMRepeatAddress:
		{
			// There is a short window of time here between the voice being keyed on and the first block finishing decoding
			// where setting the repeat address will *NOT* ignore the block/loop start flag. Games sensitive to this are:
			//  - The Misadventures of Tron Bonne
			//  - Re-Loaded - The Hardcore Sequel
			//  - Valkyrie Profile

			voice.ignoreLoopAddress |= ( voice.IsOn() && !voice.firstBlock );
			voice.registers.adpcmRepeatAddress = value;
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

	if ( newControl.soundRamTransferMode != m_control.soundRamTransferMode )
	{
		m_transferEvent->UpdateEarly();

		// duckstation clears the fifo on stop
		if ( newControl.GetTransfermode() == TransferMode::Stop )
			m_transferBuffer.Clear();
	}

	if ( !newControl.enable && m_control.enable )
	{
		for ( auto& voice : m_voices )
			voice.ForceOff();
	}

	m_control.value = value;
	m_status.value = value & Status::ControlMask; // SPUSTAT bits 0-5 are the same as SPUCNT, but applied with a delay (delay not required)

	if ( !newControl.irqEnable )
	{
		// acknowledge IRQ
		m_status.irq = false;
	}
	else
	{
		m_transferEvent->UpdateEarly();
		CheckForLateInterrupt();
	}

	ScheduleGenerateSamplesEvent();
	UpdateDmaRequest();
	ScheduleTransferEvent();
}

inline void Spu::TriggerInterrupt() noexcept
{
	dbExpects( CanTriggerInterrupt() );
	m_status.irq = true;
	m_interruptControl.SetInterrupt( Interrupt::Spu );
}

void Spu::CheckForLateInterrupt() noexcept
{
	if ( !CanTriggerInterrupt() )
		return;

	if ( CheckIrqAddress( m_transferAddress ) )
	{
		TriggerInterrupt();
		return;
	}

	for ( uint32_t i = 0; i < VoiceCount; ++i )
	{
		const auto& voice = m_voices[ i ];

		// from Duckstation:
		// we skip voices which haven't started this block yet - because they'll check
		// the next time they're sampled, and the delay might be important.
		if ( !voice.hasSamples )
			continue;

		const uint32_t address = voice.currentAddress * 8;
		if ( CheckIrqAddress( address ) || CheckIrqAddress( ( address + 8 ) & SpuRamAddressMask ) )
		{
			TriggerInterrupt();
			return;
		}
	}
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
			request = m_transferBuffer.Full();
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
	if ( m_control.GetTransfermode() == TransferMode::DMARead )
	{
		while ( !m_transferBuffer.Full() && cycles > 0 )
		{
			m_transferBuffer.Push( m_ram.Read<uint16_t>( m_transferAddress ) );
			m_transferAddress = ( m_transferAddress + 2 ) & SpuRamAddressMask;
			cycles -= TransferCyclesPerHalfword;
			TryTriggerInterrupt( m_transferAddress );
		}
	}
	else
	{
		while ( !m_transferBuffer.Empty() && cycles > 0 )
		{
			m_ram.Write( m_transferAddress, m_transferBuffer.Pop() );
			m_transferAddress = ( m_transferAddress + 2 ) & SpuRamAddressMask;
			cycles -= TransferCyclesPerHalfword;
			TryTriggerInterrupt( m_transferAddress );
		}
	}

	// wait for a DMA before transfering more data
	UpdateDmaRequest();
	ScheduleTransferEvent();
}

void Spu::ScheduleGenerateSamplesEvent() noexcept
{
	const uint32_t framesForQueue = std::min<uint32_t>( m_audioQueue.Capacity() / 2, m_audioQueue.GetDeviceBufferSize() ); // two samples per frame
	const uint32_t batchFrames = ( m_control.enable && m_control.irqEnable ) ? 1 : std::max<uint32_t>( framesForQueue, 1 );
	const cycles_t cycles = batchFrames * CyclesPerAudioFrame - m_pendingCarryCycles;
	m_generateSamplesEvent->Schedule( cycles );
}

void Spu::GeneratePendingSamples() noexcept
{
	const cycles_t pendingCycles = m_generateSamplesEvent->GetPendingCycles();
	const uint32_t pendingFrames = ( pendingCycles + m_pendingCarryCycles ) / CyclesPerAudioFrame;
	if ( pendingFrames > 0 )
	{
		m_transferEvent->UpdateEarly();
		m_generateSamplesEvent->UpdateEarly();
	}
}

void Spu::GenerateSamples( cycles_t cycles ) noexcept
{
	uint32_t remainingFrames = ( cycles + m_pendingCarryCycles ) / CyclesPerAudioFrame;
	m_pendingCarryCycles = ( cycles + m_pendingCarryCycles ) % CyclesPerAudioFrame;

	m_generatedFrames += remainingFrames;

	while ( remainingFrames > 0 )
	{
		auto writer = m_audioQueue.GetBatchWriter();
		const size_t batchFrames = std::min( remainingFrames, writer.GetBatchSize() / 2 );

		for ( uint32_t i = 0; i < batchFrames; ++i )
		{
			int32_t leftSum = 0;
			int32_t rightSum = 0;

			int32_t reverbInLeft = 0;
			int32_t reverbInRight = 0;

			// mix in voices
			for ( uint32_t voiceIndex = 0; voiceIndex < VoiceCount; ++voiceIndex )
			{
				const auto [left, right] = SampleVoice( voiceIndex );
				leftSum += left;
				rightSum += right;

				if ( m_voiceFlags.reverbEnable & ( 1 << voiceIndex ) )
				{
					reverbInLeft += left;
					reverbInRight += right;
				}
			}

			if ( !m_control.unmute )
			{
				leftSum = 0;
				rightSum = 0;
			}

			UpdateNoise();

			// mix in CD audio
			const auto [cdSampleLeft, cdSampleRight] = m_cdromDrive.GetAudioFrame();
			if ( m_control.cdAudioEnable )
			{
				const int32_t cdVolumeLeft = ApplyVolume( cdSampleLeft, m_cdAudioInputVolume[ 0 ] );
				const int32_t cdVolumeRight = ApplyVolume( cdSampleRight, m_cdAudioInputVolume[ 1 ] );

				leftSum += cdVolumeLeft;
				rightSum += cdVolumeRight;

				if ( m_control.cdAudioReverb )
				{
					reverbInLeft += cdVolumeLeft;
					reverbInRight += cdVolumeRight;
				}
			}

			// process and mix in reverb
			/*
			const auto [reverbOutLeft, reverbOutRight] = ProcessReverb( SaturateSample( reverbInLeft ), SaturateSample( reverbInRight ) );
			leftSum += reverbOutLeft;
			rightSum += reverbOutRight;
			*/
			(void)reverbInLeft, reverbInRight; // TEMP

			const int16_t outputLeft = static_cast<int16_t>( ApplyVolume( SaturateSample( leftSum ), m_mainVolume[ 0 ].currentLevel ) );
			const int16_t outputRight = static_cast<int16_t>( ApplyVolume( SaturateSample( rightSum ), m_mainVolume[ 1 ].currentLevel ) );
			m_mainVolume[ 0 ].Tick();
			m_mainVolume[ 1 ].Tick();

			writer.PushSample( outputLeft );
			writer.PushSample( outputRight );

			WriteToCaptureBuffer( 0, cdSampleLeft );
			WriteToCaptureBuffer( 1, cdSampleRight );
			WriteToCaptureBuffer( 2, SaturateSample( m_voices[ 1 ].lastVolume ) );
			WriteToCaptureBuffer( 3, SaturateSample( m_voices[ 3 ].lastVolume ) );

			m_captureBufferPosition = ( m_captureBufferPosition + 2 ) % CaptureBufferSize;
			m_status.writingToCaptureBufferHalf = ( m_captureBufferPosition >= ( CaptureBufferSize / 2 ) );

			// duckstation keys voices AFTER the first processed frame
			if ( i == 0 && ( m_voiceFlags.keyOn != 0 || m_voiceFlags.keyOff != 0 ) )
				KeyVoices();
		}

		remainingFrames -= batchFrames;
	}

	ScheduleGenerateSamplesEvent();
}

void Spu::KeyVoices() noexcept
{
	const uint32_t keyOn = std::exchange( m_voiceFlags.keyOn, 0 );
	const uint32_t keyOff = std::exchange( m_voiceFlags.keyOff, 0 );

	for ( uint32_t i = 0; i < VoiceCount; ++i )
	{
		const uint32_t voiceFlag = 1u << i;

		if ( keyOff & voiceFlag )
			m_voices[ i ].KeyOff();

		if ( keyOn & voiceFlag )
		{
			m_voiceFlags.endx &= ~voiceFlag; // key on clears endx flag
			m_voices[ i ].KeyOn();
		}
	}
}

std::pair<int32_t, int32_t> Spu::SampleVoice( uint32_t voiceIndex ) noexcept
{
	auto& voice = m_voices[ voiceIndex ];

	if ( !voice.IsOn() && !m_control.irqEnable )
	{
		voice.lastVolume = 0;
		return { 0, 0 };
	}

	if ( !voice.hasSamples )
	{
		const ADPCMBlock block = ReadADPCMBlock( voice.currentAddress );
		voice.DecodeBlock( block );

		if ( voice.currentBlockFlags.loopStart && !voice.ignoreLoopAddress )
			voice.registers.adpcmRepeatAddress = voice.currentAddress;
	}

	const uint32_t voiceFlag = 1u << voiceIndex;

	int32_t volume = 0;
	if ( voice.registers.currentADSRVolume != 0 )
	{
		const int32_t sample = ( m_voiceFlags.noiseModeEnable & voiceFlag ) ? GetCurrentNoiseLevel() : voice.Interpolate();
		volume = ApplyVolume( sample, voice.registers.currentADSRVolume );
	}

	voice.lastVolume = volume;

	if ( voice.adsrPhase != ADSRPhase::Off )
		voice.TickADSR();

	// pitch modulation
	uint16_t step = voice.registers.adpcmSampleRate;
	if ( ( voiceIndex > 0 ) && ( m_voiceFlags.pitchModulationEnable & voiceFlag ) )
	{
		const int32_t factor = std::clamp<int32_t>( m_voices[ voiceIndex - 1 ].lastVolume, std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max() ) + 0x8000;
		step = static_cast<uint16_t>( ( static_cast<int16_t>( step ) * factor ) >> 15 );
	}
	step = std::min<uint16_t>( step, 0x3fff );

	// from Duckstation:
	// Shouldn't ever overflow because if sample_index == 27, step == 0x4000 there won't be a carry out from the
	// interpolation index. If there is a carry out, bit 12 will never be 1, so it'll never add more than 4 to
	// sample_index, which should never be >27.
	dbAssert( voice.counter.sampleIndex < SamplesPerADPCMBlock );
	voice.counter.value += static_cast<uint32_t>( step );

	if ( voice.counter.sampleIndex >= SamplesPerADPCMBlock )
	{
		voice.counter.sampleIndex -= SamplesPerADPCMBlock;
		voice.hasSamples = false;
		voice.firstBlock = false;
		voice.currentAddress += 2;

		if ( voice.currentBlockFlags.loopEnd )
		{
			m_voiceFlags.endx |= voiceFlag;
			voice.currentAddress = voice.registers.adpcmRepeatAddress & ~1u;

			if ( !voice.currentBlockFlags.loopRepeat )
				voice.ForceOff();
		}
	}

	const int32_t left = ApplyVolume( volume, voice.volume[ 0 ].currentLevel );
	const int32_t right = ApplyVolume( volume, voice.volume[ 1 ].currentLevel );
	voice.volume[ 0 ].Tick();
	voice.volume[ 1 ].Tick();

	return { left, right };
}

Spu::ADPCMBlock Spu::ReadADPCMBlock( uint16_t address ) noexcept
{
	ADPCMBlock block;

	uint32_t curAddress = ( address * 8 ) & SpuRamAddressMask;

	if ( CanTriggerInterrupt() && ( CheckIrqAddress( curAddress ) || CheckIrqAddress( ( curAddress + 8 ) & SpuRamAddressMask ) ) )
		TriggerInterrupt();

	if ( curAddress + sizeof( ADPCMBlock ) <= SpuRamSize )
	{
		// no wrapping, simply copy

		std::memcpy( &block, m_ram.Data() + curAddress, sizeof( ADPCMBlock ) );
	}
	else
	{
		// wrapping

		auto incrementAddress = [&curAddress] { curAddress = ( curAddress + 1 ) & SpuRamAddressMask; };

		block.header.value = m_ram[ curAddress ];
		incrementAddress();

		block.flags.value = m_ram[ curAddress ];
		incrementAddress();

		for ( uint32_t i = 0; i < block.data.size(); ++i )
		{
			block.data[ i ] = m_ram[ curAddress ];
			incrementAddress();
		}
	}

	return block;
}

void Spu::UpdateNoise() noexcept
{
	// Dr Hell's noise waveform, implementation taken from Duckstation

	static constexpr std::array<uint8_t, 64> NoiseWaveAdd
	{
		1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0,
		0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1
	};

	static constexpr std::array<uint8_t, 5> NoiseFrequencyAdd = { 0, 84, 140, 180, 210 };

	const uint32_t noiseClock = m_control.noiseFrequencyRate;
	const uint32_t level = ( 0x8000u >> ( noiseClock >> 2 ) ) << 16;

	m_noiseCount += 0x10000u + NoiseFrequencyAdd[ noiseClock & 3u ];

	if ( ( m_noiseCount & 0xffffu ) >= NoiseFrequencyAdd[ 4 ] )
	{
		m_noiseCount += 0x10000;
		m_noiseCount -= NoiseFrequencyAdd[ noiseClock & 3u ];
	}

	if ( m_noiseCount < level )
		return;

	m_noiseCount %= level;
	m_noiseLevel = ( m_noiseLevel << 1 ) | NoiseWaveAdd[ ( m_noiseLevel >> 10 ) & 63u ];
}

void Spu::WriteToCaptureBuffer( uint32_t index, int16_t sample ) noexcept
{
	const uint32_t address = ( index * CaptureBufferSize ) | m_captureBufferPosition;
	m_ram.Write<uint16_t>( address, sample );
	TryTriggerInterrupt( address );
}

///////////////////////////////////////////////////////////////////////
// Reverb algorithm taken from Duckstation (taken from Mednafen-PSX) //
///////////////////////////////////////////////////////////////////////

uint32_t Spu::ReverbMemoryAddress( uint32_t address ) const noexcept
{
	// Ensures address does not leave the reverb work area.
	static constexpr uint32_t MASK = ( SpuRamSize - 1 ) / 2;
	uint32_t offset = m_reverbCurrentAddress + ( address & MASK );
	offset += m_reverbBaseAddress & ( static_cast<int32_t>( offset << 13 ) >> 31 );

	// We address RAM in bytes
	return ( offset & MASK ) * 2u;
}

int16_t Spu::ReverbRead( uint32_t address, int32_t offset ) noexcept
{
	// TODO: This should check interrupts.
	const uint32_t realAddress = ReverbMemoryAddress( ( address << 2 ) + offset );
	return m_ram.Read<int16_t>( realAddress );
}

void Spu::ReverbWrite( uint32_t address, int16_t data ) noexcept
{
	// TODO: This should check interrupts.
	const uint32_t realAddress = ReverbMemoryAddress( address << 2 );
	m_ram.Write<int16_t>( realAddress, data );
}

std::pair<int32_t, int32_t> Spu::ProcessReverb( int16_t inLeft, int16_t inRight ) noexcept
{
	m_reverbDownsampleBuffer[ 0 ][ m_reverbResampleBufferPosition | 0x00 ] = inLeft;
	m_reverbDownsampleBuffer[ 0 ][ m_reverbResampleBufferPosition | 0x40 ] = inLeft;
	m_reverbDownsampleBuffer[ 1 ][ m_reverbResampleBufferPosition | 0x00 ] = inRight;
	m_reverbDownsampleBuffer[ 1 ][ m_reverbResampleBufferPosition | 0x40 ] = inRight;

	std::array<int32_t, 2> out;
	if ( m_reverbResampleBufferPosition & 1u )
	{
		std::array<int32_t, 2> downsampled;
		for ( unsigned lr = 0; lr < 2; lr++ )
			downsampled[ lr ] = Reverb4422( &m_reverbDownsampleBuffer[ lr ][ ( m_reverbResampleBufferPosition - 38 ) & 0x3F ] );

		for ( unsigned lr = 0; lr < 2; lr++ )
		{
			if ( m_control.reverbMasterEnable )
			{
				const int16_t IIR_INPUT_A =
					ReverbSat( ( ( ( ReverbRead( m_reverb.sameSideReflectionAddress2[ lr ^ 0 ] ) * m_reverb.reflectionVolume2 ) >> 14 ) +
						( ( downsampled[ lr ] * m_reverb.inputVolume[ lr ] ) >> 14 ) ) >>
						1 );
				const int16_t IIR_INPUT_B =
					ReverbSat( ( ( ( ReverbRead( m_reverb.differentSideReflectionAddress2[ lr ^ 1 ] ) * m_reverb.reflectionVolume2 ) >> 14 ) +
						( ( downsampled[ lr ] * m_reverb.inputVolume[ lr ] ) >> 14 ) ) >>
						1 );
				const int16_t IIR_A =
					ReverbSat( ( ( ( IIR_INPUT_A * m_reverb.reflectionVolume1 ) >> 14 ) +
						( IIASM( m_reverb.reflectionVolume1, ReverbRead( m_reverb.sameSideReflectionAddress1[ lr ], -1 ) ) >> 14 ) ) >>
						1 );
				const int16_t IIR_B =
					ReverbSat( ( ( ( IIR_INPUT_B * m_reverb.reflectionVolume1 ) >> 14 ) +
						( IIASM( m_reverb.reflectionVolume1, ReverbRead( m_reverb.differentSideReflectionAddress1[ lr ], -1 ) ) >> 14 ) ) >>
						1 );

				ReverbWrite( m_reverb.sameSideReflectionAddress1[ lr ], IIR_A );
				ReverbWrite( m_reverb.differentSideReflectionAddress1[ lr ], IIR_B );
			}

			const int32_t ACC = ( ( ReverbRead( m_reverb.combAddress1[ lr ] ) * m_reverb.combVolume1 ) >> 14 ) +
				( ( ReverbRead( m_reverb.combAddress2[ lr ] ) * m_reverb.combVolume2 ) >> 14 ) +
				( ( ReverbRead( m_reverb.combAddress3[ lr ] ) * m_reverb.combVolume3 ) >> 14 ) +
				( ( ReverbRead( m_reverb.combAddress4[ lr ] ) * m_reverb.combVolume4 ) >> 14 );

			const int16_t FB_A = ReverbRead( m_reverb.apfAddress1[ lr ] - m_reverb.apfOffset1 );
			const int16_t FB_B = ReverbRead( m_reverb.apfAddress2[ lr ] - m_reverb.apfOffset2 );
			const int16_t MDA = ReverbSat( ( ACC + ( ( FB_A * ReverbNeg( m_reverb.apfVolume1 ) ) >> 14 ) ) >> 1 );
			const int16_t MDB = ReverbSat(
				FB_A +
				( ( ( ( MDA * m_reverb.apfVolume1 ) >> 14 ) + ( ( FB_B * ReverbNeg( m_reverb.apfVolume2 ) ) >> 14 ) ) >> 1 ) );
			const int16_t IVB = ReverbSat( FB_B + ( ( MDB * m_reverb.apfVolume2 ) >> 15 ) );

			if ( m_control.reverbMasterEnable )
			{
				ReverbWrite( m_reverb.apfAddress1[ lr ], MDA );
				ReverbWrite( m_reverb.apfAddress2[ lr ], MDB );
			}

			m_reverbUpsampleBuffer[ lr ][ ( m_reverbResampleBufferPosition >> 1 ) | 0x20 ] =
				m_reverbUpsampleBuffer[ lr ][ m_reverbResampleBufferPosition >> 1 ] = IVB;
		}

		m_reverbCurrentAddress = ( m_reverbCurrentAddress + 1 ) & 0x3FFFFu;
		if ( m_reverbCurrentAddress == 0 )
			m_reverbCurrentAddress = m_reverbBaseAddress;

		for ( unsigned lr = 0; lr < 2; lr++ )
			out[ lr ] =
			Reverb2244<false>( &m_reverbUpsampleBuffer[ lr ][ ( ( m_reverbResampleBufferPosition >> 1 ) - 19 ) & 0x1F ] );
	}
	else
	{
		for ( unsigned lr = 0; lr < 2; lr++ )
			out[ lr ] = Reverb2244<true>( &m_reverbUpsampleBuffer[ lr ][ ( ( m_reverbResampleBufferPosition >> 1 ) - 19 ) & 0x1F ] );
	}

	m_reverbResampleBufferPosition = ( m_reverbResampleBufferPosition + 1 ) & 0x3F;

	const int32_t outLeft = ApplyVolume( out[ 0 ], m_reverbOutVolume[ 0 ] );
	const int32_t outRight = ApplyVolume( out[ 1 ], m_reverbOutVolume[ 1 ] );
	return { outLeft, outRight };
}

} // namespace PSX