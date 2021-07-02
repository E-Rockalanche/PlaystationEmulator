#include "DMA.h"

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
		case 29:	return m_interruptRegister;

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
			const uint32_t irqFlags = ( m_interruptRegister & ~value ) & InterruptRegister::IrqFlagsMask; // write 1 to reset irq flags
			m_interruptRegister = ( value & InterruptRegister::WriteMask ) | irqFlags;

			if ( GetForceIrq() || ( GetIrqMasterEnable() && ( GetIrqEnables() & GetIrqFlags() ) != 0 ) )
			{
				m_interruptRegister |= InterruptRegister::IrqMasterFlag;
				// TODO: Upon 0-to-1 transition of Bit31, the IRQ3 flag (in Port 1F801070h) gets set.
			}

			break;
		}

		case 30:
		case 31:
			break;
	}
}

void Dma::DoBlockTransfer( uint32_t channelIndex ) noexcept
{
	auto& channel = m_channels[ channelIndex ];

	const bool toRam = channel.GetTransferDirection() == Channel::TransferDirection::ToMainRam;
	dbLog( "Dma::DoBlockTransfer() -- channel: %u, direction: %s", channelIndex, toRam ? "to RAM" : "from RAM" );

	const int32_t increment = ( channel.GetMemoryAddressStep() == Channel::MemoryAddressStep::Forward ) ? 4 : -4;

	uint32_t address = channel.GetBaseAddress();
	dbAssert( address % 4 == 0 );
	// TODO: address might need to be bitwise anded with 0x001ffffc

	uint32_t wordCount = channel.GetWordCount();
	dbAssert( wordCount != 0 );

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
					m_ram.Write<uint32_t>( address, 0 );

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

	channel.SetTransferComplete();
}

void Dma::DoLinkedListTransfer( uint32_t channelIndex ) noexcept
{
	dbLog( "Dma::DoLinkedListTransfer()" );
	dbAssert( channelIndex == ChannelIndex::Gpu ); // must transfer to GPU

	auto& channel = m_channels[ channelIndex ];
	dbAssert( channel.GetTransferDirection() == Channel::TransferDirection::FromMainRam ); // must transfer from RAM

	uint32_t address = channel.GetBaseAddress();
	dbAssert( address % 4 == 0 );

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
	}
	while ( address != 0x00ffffff );

	channel.SetTransferComplete();
}

}