#include "DMA.h"

#include "CDRomDrive.h"
#include "EventManager.h"
#include "GPU.h"
#include "InterruptControl.h"
#include "MacroblockDecoder.h"

#include <stdx/bit.h>

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

void Dma::Reset()
{
	for ( auto& channel : m_channels )
		channel = {};

	m_controlRegister = ControlRegisterResetValue;
	m_interruptRegister.value = 0;
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
			switch ( static_cast<ChannelRegister>( registerIndex ) )
			{
				case ChannelRegister::BaseAddress:		return state.baseAddress;
				case ChannelRegister::BlockControl:		return static_cast<uint32_t>( state.wordCount | ( state.blockCount << 16 ) );
				case ChannelRegister::ChannelControl:	return state.control.value;
				default:
					dbLogWarning( "Dma::Read -- invalid channel register" );
					return 0xffffffffu;
			}
		}

		case Register::Control:
			return m_controlRegister;

		case Register::Interrupt:
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
					state.baseAddress = value & 0x00ffffff;
					break;

				case ChannelRegister::BlockControl:
					state.wordCount = static_cast<uint16_t>( value );
					state.blockCount = static_cast<uint16_t>( value >> 16 );
					break;

				case ChannelRegister::ChannelControl:
				{
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
			m_controlRegister = value;
			break;

		case Register::Interrupt:
		{
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

void Dma::StartDma( Channel channel )
{
	auto& state = m_channels[ static_cast<uint32_t>( channel ) ];
	state.control.startTrigger = false;

	const uint32_t startAddress = state.baseAddress;
	const bool toRam = state.control.transferDirection == false;
	const uint32_t addressStep = state.control.memoryAddressStep ? BackwardStep : ForwardStep;

	switch ( state.GetSyncMode() )
	{
		case SyncMode::Manual:
		{
			const uint32_t words = state.GetWordCount();

			dbLogDebug( "Dma::StartDma -- Manual [channel: %s, toRam: %i, address: %X, words: %X, step: %i", ChannelNames[ (size_t)channel ], toRam, startAddress, words, (int32_t)addressStep );

			// TODO: chopping
			if ( toRam )
				TransferToRam( channel, startAddress & DmaAddressMask, words, addressStep );
			else
				TransferFromRam( channel, startAddress & DmaAddressMask, words, addressStep );

			AddBulkCycles( GetCyclesForTransfer( words ) );
			break;
		}

		case SyncMode::Request:
		{
			const uint32_t blockSize = state.GetBlockSize();
			uint32_t blocksRemaining = state.GetBlockCount();
			uint32_t currentAddress = startAddress;

			dbLogDebug( "Dma::StartDma -- Request [channel: %s, toRam: %i, address: %X, blocks: %X, blockSize: %X, step: %i", ChannelNames[ (size_t)channel ], toRam, startAddress, blocksRemaining, blockSize, (int32_t)addressStep );

			while ( state.request && blocksRemaining > 0 )
			{
				if ( toRam )
					TransferToRam( channel, currentAddress & DmaAddressMask, blockSize, addressStep );
				else
					TransferFromRam( channel, currentAddress & DmaAddressMask, blockSize, addressStep );

				currentAddress += blockSize * addressStep;
				--blocksRemaining;
				AddBulkCycles( GetCyclesForTransfer( blockSize ) );
			}

			state.SetBaseAddress( currentAddress );
			state.blockCount = static_cast<uint16_t>( blocksRemaining );

			if ( blocksRemaining > 0 )
			{
				if ( state.request )
					dbLogWarning( "Dma::StartDma -- Request stopped unexpectedly" );

				return;
			}

			break;
		}

		case SyncMode::LinkedList:
		{
			if ( toRam )
			{
				dbLogWarning( "Dma::StartDma -- cannot do linked list transfer to ram" );
				return;
			}

			uint32_t totalWords = 0;
			uint32_t currentAddress = state.baseAddress;

			dbLogDebug( "Dma::StartDma -- LinkedList [channel: %s, address: %X]", ChannelNames[ (size_t)channel ], currentAddress );

			do
			{
				uint32_t header = m_ram.Read<uint32_t>( currentAddress & DmaAddressMask );
				const uint32_t wordCount = header >> 24;
				if ( wordCount > 0 )
					TransferFromRam( channel, ( currentAddress + 4 ) & DmaAddressMask, wordCount, ForwardStep );

				currentAddress = header & 0x00ffffffu;
				totalWords += wordCount + 1; // +1 for header?
			}
			while ( currentAddress != LinkedListTerminator );

			state.SetBaseAddress( LinkedListTerminator );
			AddBulkCycles( GetCyclesForTransfer( totalWords ) );
			break;
		}
	}

	FinishTransfer( channel );
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

void Dma::TransferToRam( const Channel channel, const uint32_t address, const uint32_t wordCount, const uint32_t addressStep )
{
	dbExpects( address < RamSize );
	dbExpects( wordCount > 0 );
	dbExpects( addressStep == ForwardStep || addressStep == BackwardStep );

	if ( channel == Channel::RamOrderTable )
	{
		ClearOrderTable( address, wordCount );
		return;
	}

	uint32_t* dest = reinterpret_cast<uint32_t*>( m_ram.Data() + address );

	const bool useTempBuffer = ( addressStep == BackwardStep ) || ( address + wordCount * 4 > RamSize ); // backward step or wrapping
	if ( useTempBuffer )
	{
		ResizeTempBuffer( wordCount );
		dest = m_tempBuffer.get();
	}

	switch ( channel )
	{
		case Channel::MDecOut:
		{
			m_mdec.DmaOut( dest, wordCount );
			break;
		}

		case Channel::Gpu:
		{
			for ( uint32_t i = 0; i < wordCount; ++i )
			{
				dest[ i ] = m_gpu.GpuRead();
			}
			break;
		}

		case Channel::CdRom:
		{
			m_cdromDrive.DmaRead( dest, wordCount );
			break;
		}

		case Channel::Spu:
		{
			// TODO
			dbLogDebug( "ignoring SPU to RAM DMA transfer" );
			break;
		}

		default:
			dbLogWarning( "Dma::TransferToRam -- invalid channel [%X]", channel );
			std::fill_n( dest, wordCount, 0xffffffffu );
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


void Dma::TransferFromRam( const Channel channel, const uint32_t address, const uint32_t wordCount, const uint32_t addressStep )
{
	dbExpects( address < RamSize );
	dbExpects( wordCount > 0 );
	dbExpects( addressStep == ForwardStep || addressStep == BackwardStep );

	const uint32_t* src = reinterpret_cast<const uint32_t*>( m_ram.Data() + address );

	if ( ( addressStep == BackwardStep ) || ( address + wordCount * 4 > RamSize ) )
	{
		// backward step or wrapping

		ResizeTempBuffer( wordCount );

		uint32_t curAddress = address;
		for ( uint32_t i = 0; i < wordCount; ++i )
		{
			m_tempBuffer[ i ] = m_ram.Read<uint32_t>( curAddress );
			curAddress = ( address + addressStep ) & DmaAddressMask;
		}

		src = m_tempBuffer.get();
	}

	switch ( channel )
	{
		case Channel::MDecIn:
		{
			m_mdec.DmaIn( src, wordCount );
			break;
		}

		case Channel::Gpu:
		{
			for ( uint32_t i = 0; i < wordCount; ++i )
			{
				m_gpu.WriteGP0( src[ i ] );
			}
			break;
		}

		case Channel::Spu:
		{
			// TODO
			dbLogDebug( "ignoring RAM to SPU DMA transfer" );
			break;
		}

		default:
			dbLogWarning( "Dma::TransferFromRam -- invalid channel [%X]", channel );
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

void Dma::AddBulkCycles( cycles_t cycles )
{
	m_eventManager.AddCycles( cycles );
	if ( m_eventManager.ReadyForNextEvent() )
		m_eventManager.UpdateNextEvent();
}

}