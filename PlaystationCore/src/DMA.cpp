#include "DMA.h"

#include "CDRomDrive.h"
#include "EventManager.h"
#include "GPU.h"
#include "InterruptControl.h"
#include "MacroblockDecoder.h"
#include "SaveState.h"
#include "SPU.h"

#include <stdx/bit.h>

#define DMA_TRACE( ... ) dbLogDebug( __VA_ARGS__ )

namespace PSX
{

namespace
{

const std::array<const char*, 7> ChannelNames
{
	"MDEC_IN",
	"MDEC_OUT",
	"GPU",
	"CDROM",
	"SPU",
	"PIO",
	"OTC"
};

}

Dma::Dma( Ram& ram,
	Gpu& gpu,
	CDRomDrive& cdromDRive,
	MacroblockDecoder& mdec,
	Spu& spu,
	InterruptControl& interruptControl,
	EventManager& eventManager )
	: m_ram{ ram }
	, m_gpu{ gpu }
	, m_cdromDrive{ cdromDRive }
	, m_mdec{ mdec }
	, m_spu{ spu }
	, m_interruptControl{ interruptControl }
	, m_eventManager{ eventManager }
{
	m_resumeDmaEvent = eventManager.CreateEvent( "DMA Resume Event", [this]( cycles_t ) { ResumeDma(); } );
}

void Dma::Reset()
{
	m_resumeDmaEvent->Reset();

	for ( auto& channel : m_channels )
		channel = {};

	m_controlRegister = ControlRegisterResetValue;
	m_interruptRegister.value = 0;
	m_tempBuffer.clear();
}

uint32_t Dma::Read( uint32_t index ) const noexcept
{
	dbExpects( index < 32 );
	switch ( static_cast<Register>( index ) )
	{
		default:
		{
			const uint32_t channelIndex = index / 4;
			const uint32_t registerIndex = index % 4;

			if ( channelIndex >= m_channels.size() )
			{
				dbLogWarning( "Dma::Read -- invalid channel" );
				return 0xffffffffu;
			}

			auto& state = m_channels[ channelIndex ];

			uint32_t value;
			switch ( static_cast<ChannelRegister>( registerIndex ) )
			{
				case ChannelRegister::BaseAddress:
					value =  state.baseAddress;
					DMA_TRACE( "Dma::Read -- channel %u base address [%08X]", channelIndex, value );
					break;

				case ChannelRegister::BlockControl:
					value = static_cast<uint32_t>( state.wordCount ) | ( static_cast<uint32_t>( state.blockCount ) << 16 );
					DMA_TRACE( "Dma::Read -- channel %u block control [%08X]", channelIndex, value );
					break;

				case ChannelRegister::ChannelControl:
					value = state.control.value;
					// reads from channel 2 frequently
					// DMA_TRACE( "Dma::Read -- channel %u channel control [%08X]", channelIndex, value );
					break;

				default:
					dbLogWarning( "Dma::Read -- invalid channel register" );
					value = 0xffffffffu;
					break;
			}
			return value;
		}

		case Register::Control:
			DMA_TRACE( "Dma::Read -- control [%08X]", m_controlRegister );
			return m_controlRegister;

		case Register::Interrupt:
			DMA_TRACE( "Dma::Read -- interrupt [%08X]", m_interruptRegister.value );
			return m_interruptRegister.value;

		case Register::Unknown1:
			dbLogWarning( "Dma::Read -- reading from unused register" );
			return 0x7ffac68b;

		case Register::Unknown2:
			dbLogWarning( "Dma::Read -- reading from unused register" );
			return 0x00fffff7;
	}
}

void Dma::Write( uint32_t index, uint32_t value ) noexcept
{
	dbExpects( index < 32 );
	switch ( static_cast<Register>( index ) )
	{
		default:
		{
			const uint32_t channelIndex = index / 4;
			const uint32_t registerIndex = index % 4;

			if ( channelIndex >= m_channels.size() )
			{
				dbLogWarning( "Dma::Write -- invalid channel" );
				return;
			}

			auto& state = m_channels[ channelIndex ];
			switch ( static_cast<ChannelRegister>( registerIndex ) )
			{
				case ChannelRegister::BaseAddress:
					state.SetBaseAddress( value );
					DMA_TRACE( "Dma::Write -- channel %u base address [%08X]", channelIndex, state.baseAddress );
					break;

				case ChannelRegister::BlockControl:
					DMA_TRACE( "Dma::Write -- channel %u block control [%08X]", channelIndex, value );
					state.wordCount = static_cast<uint16_t>( value );
					state.blockCount = static_cast<uint16_t>( value >> 16 );
					break;

				case ChannelRegister::ChannelControl:
				{
					DMA_TRACE( "Dma::Write -- channel %u channel control [%08X]", channelIndex, value );

					const Channel channel = static_cast<Channel>( channelIndex );

					if ( channel == Channel::RamOrderTable )
						state.control.value = ( value & 0x51000000 ) | 0x00000002; // only bits 24, 28, and 30 of OTC are writeable. bit 1 is always 1 (address step backwards)
					else
						state.control.value = value & ChannelState::Control::WriteMask;

					if ( CanTransferChannel( channel ) )
						StartDma( channel );

					break;
				}

				default:
					dbLogWarning( "Dma::Write -- invalid channel register" );
					break;
			}
			break;
		}

		case Register::Control:
			DMA_TRACE( "Dma::Write -- control [%08X]", value );
			m_controlRegister = value;
			break;

		case Register::Interrupt:
		{
			DMA_TRACE( "Dma::Write -- interrupt [%08X]", value );

			const bool oldIrqMasterFlag = m_interruptRegister.irqMasterFlag;

			// Bit24-30 are acknowledged (reset to zero) when writing a "1" to that bits (and, additionally, IRQ3 must be acknowledged via Port 1F801070h).
			const uint32_t irqFlags = ( m_interruptRegister.value & ~value ) & InterruptRegister::IrqFlagsMask;
			m_interruptRegister.value = ( value & InterruptRegister::WriteMask ) | irqFlags;
			m_interruptRegister.UpdateIrqMasterFlag();

			// Upon 0-to-1 transition of Bit31, the IRQ3 flag (in Port 1F801070h) gets set.
			if ( !oldIrqMasterFlag && m_interruptRegister.irqMasterFlag )
				m_interruptControl.SetInterrupt( Interrupt::Dma );

			break;
		}

		case Register::Unknown1:
		case Register::Unknown2:
			dbLogWarning( "Dma::Write -- writing to unused register" );
			break;
	}
}

void Dma::SetRequest( Channel channel, bool request ) noexcept
{
	auto& state = m_channels[ static_cast<uint32_t>( channel ) ];
	if ( state.request != request )
	{
		state.request = request;

		if ( CanTransferChannel( channel ) )
			StartDma( channel );
	}
}

bool Dma::CanTransferChannel( Channel channel ) const noexcept
{
	if ( IsChannelEnabled( channel ) )
	{
		auto& state = m_channels[ static_cast<uint32_t>( channel ) ];
		return state.control.startBusy && ( state.request || state.control.startTrigger );
	}

	return false;
}

Dma::DmaResult Dma::StartDma( Channel channel )
{
	auto& state = m_channels[ static_cast<uint32_t>( channel ) ];

	dbAssert( !state.transferring );
	state.transferring = true;

	state.control.startTrigger = false;

	const uint32_t startAddress = state.baseAddress;
	const bool toRam = state.control.transferDirection == false;
	const uint32_t addressStep = state.control.memoryAddressStep ? BackwardStep : ForwardStep;

	DmaResult result = DmaResult::WaitRequest;
	cycles_t totalCycles = 0;

	switch ( state.GetSyncMode() )
	{
		case SyncMode::Manual:
		{
			const uint32_t totalWords = state.GetWordCount();

			DMA_TRACE( "Dma::StartDma -- Manual [channel: %s, toRam: %i, address: $%08X, words: $%08X, step: %i", ChannelNames[ (size_t)channel ], toRam, startAddress, totalWords, (int32_t)addressStep );

			uint32_t words = totalWords;
			result = DmaResult::Finished;
			if ( state.control.choppingEnable )
			{
				const uint32_t choppingWords = GetWordsForCycles( state.GetChoppingDmaWindowSize() );
				if ( choppingWords < words )
				{
					words = choppingWords;
					result = DmaResult::Chopping;
				}

				state.wordCount = static_cast<uint16_t>( totalWords - words );
				state.SetBaseAddress( state.baseAddress + ( words * addressStep ) );
			}

			if ( toRam )
				TransferToRam( channel, startAddress, words, addressStep );
			else
				TransferFromRam( channel, startAddress, words, addressStep );

			totalCycles += GetCyclesForWords( words );
			break;
		}

		case SyncMode::Request:
		{
			const uint32_t blockSize = state.GetBlockSize();
			const cycles_t blockCycles = GetCyclesForWords( blockSize );
			uint32_t blocksRemaining = state.GetBlockCount();
			uint32_t currentAddress = startAddress;

			DMA_TRACE( "Dma::StartDma -- Request [channel: %s, toRam: %i, address: $%08X, blocks: $%08X, blockSize: $%08X, step: %i", ChannelNames[ (size_t)channel ], toRam, startAddress, blocksRemaining, blockSize, (int32_t)addressStep );

			cycles_t remainingCycles = state.control.choppingEnable ? state.GetChoppingDmaWindowSize() : InfiniteCycles;

			while ( state.request && blocksRemaining > 0 && remainingCycles > 0 )
			{
				if ( toRam )
					TransferToRam( channel, currentAddress, blockSize, addressStep );
				else
					TransferFromRam( channel, currentAddress, blockSize, addressStep );

				currentAddress += blockSize * addressStep;
				--blocksRemaining;

				remainingCycles -= blockCycles;
				totalCycles += blockCycles;
			}
			
			state.SetBaseAddress( currentAddress );
			state.blockCount = static_cast<uint16_t>( blocksRemaining );

			if ( blocksRemaining == 0 )
			{
				result = DmaResult::Finished;
			}
			else if ( state.request )
			{
				result = DmaResult::Chopping;
			}

			break;
		}

		case SyncMode::LinkedList:
		{
			if ( toRam )
			{
				dbLogWarning( "Dma::StartDma -- cannot do linked list transfer to ram" );
				dbBreak();
				return DmaResult::Finished;
			}

			dbAssert( channel == Channel::Gpu ); // does linked list only work on the GP0?

			// cycles taken from duckstation
			static constexpr cycles_t ProcessHeaderCycles = 10;
			static constexpr cycles_t ProcessBlockCycles = 5;

			uint32_t currentAddress = state.baseAddress;

			DMA_TRACE( "Dma::StartDma -- LinkedList [channel: %s, address: $%08X]", ChannelNames[ (size_t)channel ], currentAddress );

			cycles_t remainingCycles = state.control.choppingEnable ? state.GetChoppingDmaWindowSize() : InfiniteCycles;

			while ( state.request && remainingCycles > 0 && currentAddress != LinkedListTerminator )
			{
				cycles_t curCycles = ProcessHeaderCycles;

				uint32_t header = m_ram.Read<uint32_t>( currentAddress & DmaAddressMask );
				const uint32_t wordCount = header >> 24;
				if ( wordCount > 0 )
				{
					TransferFromRam( channel, currentAddress + 4, wordCount, ForwardStep );
					curCycles += ProcessBlockCycles + GetCyclesForWords( wordCount );
				}

				currentAddress = header & 0x00ffffffu;

				totalCycles += curCycles;
				remainingCycles -= curCycles;
			}

			state.SetBaseAddress( currentAddress );

			if ( currentAddress == LinkedListTerminator )
			{
				result = DmaResult::Finished;
			}
			else if ( state.request )
			{
				result = DmaResult::Chopping;
			}

			break;
		}

		case SyncMode::Unused:
			dbBreak();
			break;
	}

	if ( totalCycles > 0 )
		m_eventManager.AddCyclesAndUpdateEvents( totalCycles );

	switch ( result )
	{
		case DmaResult::Finished:
			FinishTransfer( channel );
			break;

		case DmaResult::Chopping:
			dbBreak();
			m_resumeDmaEvent->Schedule( state.GetChoppingCpuWindowSize() );
			break;

		case DmaResult::WaitRequest:
			break;
	}

	state.transferring = false;

	return result;
}

void Dma::FinishTransfer( Channel channel ) noexcept
{
	const uint32_t channelIndex = static_cast<uint32_t>( channel );

	m_channels[ channelIndex ].control.startBusy = false;

	if ( m_interruptRegister.irqMasterEnable && ( m_interruptRegister.irqEnables & ( 1 << channelIndex ) ) )
	{
		// IRQ flags in Bit(24+n) are set upon DMAn completion - but caution - they are set ONLY if enabled in Bit(16+n).
		m_interruptRegister.irqFlags |= 1 << channelIndex;

		// Upon 0-to-1 transition of Bit31, the IRQ3 flag (in Port 1F801070h) gets set.
		if ( !m_interruptRegister.irqMasterFlag )
		{
			m_interruptRegister.irqMasterFlag = true;
			m_interruptControl.SetInterrupt( Interrupt::Dma );
		}
	}
}

void Dma::TransferToRam( Channel channel, uint32_t address, uint32_t wordCount, uint32_t addressStep )
{
	dbExpects( addressStep == ForwardStep || addressStep == BackwardStep );

	address &= DmaAddressMask;

	if ( channel == Channel::RamOrderTable )
	{
		ClearOrderTable( address, wordCount );
		return;
	}

	uint32_t* dest = reinterpret_cast<uint32_t*>( m_ram.Data() + address );

	const bool useTempBuffer = NeedsTempBuffer( address, wordCount, addressStep );
	if ( useTempBuffer )
	{
		m_tempBuffer.resize( wordCount );
		dest = m_tempBuffer.data();
	}

	switch ( channel )
	{
		case Channel::MDecOut:
			m_mdec.DmaOut( dest, wordCount );
			break;

		case Channel::Gpu:
			m_gpu.DmaOut( dest, wordCount );
			break;

		case Channel::CdRom:
			DMA_TRACE( "DMA CDROM -> RAM $%08X count=%u step=%i", address, wordCount, (int32_t)addressStep );
			m_cdromDrive.DmaRead( dest, wordCount );
			break;

		case Channel::Spu:
			m_spu.DmaRead( dest, wordCount );
			break;

		default:
			dbLogWarning( "Dma::TransferToRam -- invalid channel [%s]", ChannelNames[ (size_t)channel ] );
			dbBreak();
			// Can't pass dma chain-looping test if we fill with high bits
			// std::fill_n( dest, wordCount, 0xffffffffu );
			break;
	}

	if ( useTempBuffer )
	{
		// copy from temp buffer to ram
		uint32_t curAddress = address;
		for ( uint32_t i = 0; i < wordCount; ++i )
		{
			m_ram.Write<uint32_t>( curAddress, m_tempBuffer[ i ] );
			curAddress = ( curAddress + addressStep ) & DmaAddressMask;
		}
	}
}


void Dma::TransferFromRam( Channel channel, uint32_t address, uint32_t wordCount, uint32_t addressStep )
{
	dbExpects( addressStep == ForwardStep || addressStep == BackwardStep );

	address &= DmaAddressMask;

	const uint32_t* src = reinterpret_cast<const uint32_t*>( m_ram.Data() + address );

	if ( NeedsTempBuffer( address, wordCount, addressStep ) )
	{
		// backward step or wrapping

		m_tempBuffer.resize( wordCount );

		uint32_t curAddress = address;
		for ( uint32_t i = 0; i < wordCount; ++i )
		{
			m_tempBuffer[ i ] = m_ram.Read<uint32_t>( curAddress );
			curAddress = ( address + addressStep ) & DmaAddressMask;
		}

		src = m_tempBuffer.data();
	}

	switch ( channel )
	{
		case Channel::MDecIn:
			DMA_TRACE( "DMA RAM $%08X -> MDEC count=%u, step=%i", address, wordCount, addressStep );
			m_mdec.DmaIn( src, wordCount );
			break;

		case Channel::Gpu:
			m_gpu.DmaIn( src, wordCount );
			break;

		case Channel::Spu:
			m_spu.DmaWrite( src, wordCount );
			break;

		default:
			dbLogWarning( "Dma::TransferFromRam -- invalid channel [%s]", ChannelNames[ (size_t)channel ] );
			dbBreak();
			break;
	}
}

void Dma::ClearOrderTable( uint32_t address, uint32_t wordCount )
{
	for ( uint32_t i = 1; i < wordCount; ++i )
	{
		const uint32_t nextAddress = ( address + BackwardStep ) & DmaAddressMask;
		m_ram.Write<uint32_t>( address, nextAddress );
		address = nextAddress;
	}
	m_ram.Write<uint32_t>( address, LinkedListTerminator );
}

void Dma::ResumeDma()
{
	if ( ( m_controlRegister & 0x07777777 ) == 0x07654321 )
	{
		// default priority order

		for ( size_t i = ChannelCount; i-- > 0; )
		{
			const auto channel = static_cast<Channel>( i );
			if ( CanTransferChannel( channel ) )
			{
				if ( StartDma( channel ) == DmaResult::Chopping )
					break;
			}
		}
	}
	else
	{
		// custom priority order

		// initialize empty list of channels to resume
		struct ResumeEntry
		{
			Channel channel{};
			uint32_t priority = 0;
		};
		std::array<ResumeEntry, ChannelCount> m_resumeChannels;
		size_t resumeCount = 0;

		// insert channels that can resume
		for ( size_t i = 0; i < ChannelCount; ++i )
		{
			const auto channel = static_cast<Channel>( i );
			if ( CanTransferChannel( channel ) )
				m_resumeChannels[ resumeCount++ ] = { channel, GetChannelPriority( channel ) };
		}

		// sort channels by priority
		std::sort(
			m_resumeChannels.data(),
			m_resumeChannels.data() + resumeCount,
			[]( auto& lhs, auto& rhs )
			{
				if ( lhs.priority != rhs.priority )
					return lhs.priority > rhs.priority;
				else
					return lhs.channel > rhs.channel;
			} );

		// resume DMAs (break if chopping again)
		for ( size_t i = 0; i < resumeCount; ++i )
		{
			if ( StartDma( m_resumeChannels[ i ].channel ) == DmaResult::Chopping )
				break;
		}
	}
}

void Dma::Serialize( SaveStateSerializer& serializer )
{
	if ( !serializer.Header( "DMA", 1 ) )
		return;

	m_resumeDmaEvent->Serialize( serializer );

	for ( auto& channel : m_channels )
	{
		serializer( channel.baseAddress );
		serializer( channel.wordCount );
		serializer( channel.blockCount );
		serializer( channel.control.value );
		serializer( channel.request );
	}

	serializer( m_controlRegister );
	serializer( m_interruptRegister.value );
}

}