#include "DMA.h"

#include "CycleScheduler.h"
#include "GPU.h"
#include "InterruptControl.h"

#include <stdx/bit.h>

namespace PSX
{

void Dma::Channel::Reset()
{
	m_baseAddress = 0;
	m_blockControl = 0;
	m_channelControl = 0;
}

uint32_t Dma::Channel::Read( uint32_t index ) const noexcept
{
	dbExpects( index < 4 );
	switch ( index )
	{
		case Register::BaseAddress:		return m_baseAddress;
		case Register::BlockControl:	return m_blockControl;
		case Register::ChannelControl:	return m_channelControl;
		default:						return 0;
	}
}

void Dma::Channel::Write( uint32_t index, uint32_t value ) noexcept
{
	dbExpects( index < 4 );
	switch ( index )
	{
		case Register::BaseAddress:
			m_baseAddress = value & BaseAddressMask;
			break;

		case Register::BlockControl:
			m_blockControl = value;

		case Register::ChannelControl:
			m_channelControl = value & ChannelControl::WriteMask;
			break;
	}
}

uint32_t Dma::Channel::GetWordCount() const noexcept
{
	switch ( GetSyncMode() )
	{
		case SyncMode::Manual:
			return GetBlockSize();

		case SyncMode::Request:
			return GetBlockSize() * GetBlockCount();

		default:
			dbBreak();
			return 0;
	}
}


void Dma::Reset()
{
	for ( auto& channel : m_channels )
		channel.Reset();

	m_controlRegister = ControlRegisterResetValue;
	m_interruptRegister = 0;
}

uint32_t Dma::Read( uint32_t index ) const noexcept
{
	dbExpects( index < 32 );
	switch ( index )
	{
		default:	return m_channels[ index / 4 ].Read( index % 4 );
		case 28:	return m_controlRegister;
		case 29:	return m_interruptRegister | GetIrqMasterFlag() * InterruptRegister::IrqMasterFlag;

		// unused registers? garbage data?
		case 30:	return 0x7ffac68b;
		case 31:	return 0x00fffff7;
	}
}

void Dma::Write( uint32_t index, uint32_t value ) noexcept
{
	dbExpects( index < 32 );
	switch ( index )
	{
		default:
		{
			const uint32_t channelIndex = index / 4;
			const uint32_t registerIndex = index % 4;
			auto& channel = m_channels[ channelIndex ];
			channel.Write( registerIndex, value );
			if ( registerIndex == Channel::Register::ChannelControl && channel.Active() )
			{
				switch ( channel.GetSyncMode() )
				{
					case Channel::SyncMode::Manual:	
					case Channel::SyncMode::Request:
						DoBlockTransfer( channelIndex );
						break;

					case Channel::SyncMode::LinkedList:
						DoLinkedListTransfer( channelIndex );
						break;

					default:
						dbBreakMessage( "invalid sync mode" );
						break;
				}
			}

			break;
		}

		case 28:
			m_controlRegister = value;
			break;

		case 29:
		{
			bool oldMasterIRQ = GetIrqMasterFlag();

			const uint32_t irqFlags = ( m_interruptRegister & ~value ) & InterruptRegister::IrqFlagsMask; // write 1 to reset irq flags
			m_interruptRegister = ( value & InterruptRegister::WriteMask ) | irqFlags;

			// trigger IRQ on 0 to 1 of master flag
			if ( !oldMasterIRQ && GetIrqMasterFlag() )
				m_interruptControl.SetInterrupt( Interrupt::Dma );

			break;
		}

		case 30:
		case 31:
			break;
	}
}

void Dma::FinishTransfer( uint32_t channelIndex ) noexcept
{
	m_channels[ channelIndex ].SetTransferComplete();

	const uint32_t enableMask = InterruptRegister::IrqMasterEnable | ( 1 << ( channelIndex + 16 ) );
	if ( stdx::all_of( m_interruptRegister, enableMask ) )
	{
		// trigger IRQ on 0 to 1 of master flag
		if ( !GetIrqMasterFlag() )
			m_interruptControl.SetInterrupt( Interrupt::Dma );

		m_interruptRegister |= ( 1 << ( channelIndex + 24 ) );
	}
}

void Dma::DoBlockTransfer( uint32_t channelIndex ) noexcept
{
	auto& channel = m_channels[ channelIndex ];

	const bool toRam = channel.GetTransferDirection() == Channel::TransferDirection::ToMainRam;
	dbLog( "Dma::DoBlockTransfer() -- channel: %u, direction: %i, chopping: %i, dmaChopSize: %u, cpuChopSize: %i",
		channelIndex,
		(int)toRam,
		channel.GetChoppingEnable(),
		channel.GetChoppingDmaWindowSize(),
		channel.GetChoppingCpuWindowSize() );

	const int32_t increment = ( channel.GetMemoryAddressStep() == Channel::MemoryAddressStep::Forward ) ? 4 : -4;

	uint32_t address = channel.GetBaseAddress();
	dbAssert( address % 4 == 0 );
	// TODO: address might need to be bitwise anded with 0x001ffffc

	uint32_t wordCount = channel.GetWordCount();
	dbAssert( wordCount != 0 );

	/*
	DMA Transfer Rates
	DMA0 MDEC.IN     1 clk/word   ;0110h clks per 100h words ;\plus whatever
	DMA1 MDEC.OUT    1 clk/word   ;0110h clks per 100h words ;/decompression time
	DMA2 GPU         1 clk/word   ;0110h clks per 100h words ;-plus ...
	DMA3 CDROM/BIOS  24 clks/word ;1800h clks per 100h words ;\plus single/double
	DMA3 CDROM/GAMES 40 clks/word ;2800h clks per 100h words ;/speed sector rate
	DMA4 SPU         4 clks/word  ;0420h clks per 100h words ;-plus ...
	DMA5 PIO         20 clks/word ;1400h clks per 100h words ;-not actually used
	DMA6 OTC         1 clk/word   ;0110h clks per 100h words ;-plus nothing
	*/
	m_cycleScheduler.AddCycles( wordCount ); // TODO: be more accurate

	if ( toRam )
	{
		switch ( channelIndex )
		{
			case ChannelIndex::RamOrderTable:
			{
				dbAssert( increment == -4 ); // TODO: can it go forward?

				for ( ; wordCount > 1; --wordCount, address += increment )
					m_ram.Write<uint32_t>( address, address + increment );

				m_ram.Write<uint32_t>( address, LinkedListTerminator );
				break;
			}

			case ChannelIndex::Gpu:
			{
				// TODO
				for ( ; wordCount > 0; --wordCount, address += increment )
					m_ram.Write<uint32_t>( address, m_gpu.GpuRead() );

				break;
			}

			default:
				dbBreakMessage( "unhandled DMA transfer from channel %u to RAM", channelIndex );
				break;
		}
	}
	else
	{
		switch ( channelIndex )
		{
			case ChannelIndex::Gpu:
			{
				for ( ; wordCount > 0; --wordCount, address += increment )
					m_gpu.WriteGP0( m_ram.Read<uint32_t>( address ) );

				break;
			}

			default:
				dbBreakMessage( "unhandled DMA transfer from RAM to channel %u", channelIndex );
				break;
		}
	}

	FinishTransfer( channelIndex );
}

void Dma::DoLinkedListTransfer( uint32_t channelIndex ) noexcept
{
	dbExpects( channelIndex == ChannelIndex::Gpu ); // must transfer to GPU
	auto& channel = m_channels[ channelIndex ];

	dbAssert( channel.GetTransferDirection() == Channel::TransferDirection::FromMainRam ); // must transfer from RAM

	dbLog( "Dma::DoLinkedListTransfer() -- chopping: %i, dmaChopSize: %u, cpuChopSize: %i",
		channel.GetChoppingEnable(),
		channel.GetChoppingDmaWindowSize(),
		channel.GetChoppingCpuWindowSize() );

	uint32_t address = channel.GetBaseAddress();
	dbAssert( address % 4 == 0 );

	uint32_t totalCount = 0;

	do
	{
		uint32_t header = m_ram.Read<uint32_t>( address );
		address += 4;

		const uint32_t wordCount = header >> 24;
		const uint32_t end = address + wordCount * 4;
		for ( ; address != end; address += 4 )
		{
			m_gpu.WriteGP0( m_ram.Read<uint32_t>( address ) );
		}

		address = header & 0x00ffffff;

		totalCount += wordCount;
	}
	while ( address != 0x00ffffff );

	m_cycleScheduler.AddCycles( totalCount ); // TODO: be more accurate

	FinishTransfer( channelIndex );
}

}