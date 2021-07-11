#pragma once

#include "RAM.h"

#include <stdx/assert.h>

#include <array>
#include <cstdint>

namespace PSX
{

class Gpu;
class InterruptControl;

class Dma
{
public:
	struct ChannelIndex
	{
		enum : uint32_t
		{
			MediaDecoderInput,
			MediaDecoderOutput,
			Gpu,
			CdRom,
			Spu,
			ExtensionPort,
			RamOrderTable
		};
	};

	class Channel
	{
	public:
		struct Register
		{
			enum : uint32_t
			{
				BaseAddress,
				BlockControl,
				ChannelControl,
				Unused
			};
		};

		static constexpr uint32_t BaseAddressMask = 0x00ffffff;

		struct ChannelControl
		{
			enum : uint32_t
			{
				TransferDirection = 1u << 0,
				MemoryAddressStep = 1u << 1,
				ChoppingEnable = 1u << 8,
				SyncModeMask = 0x3u << 9,
				ChoppingDmaWindowSize = 0x7u << 16,
				ChoppingCpuWindowSize = 0x7u << 20,
				StartBusy = 1u << 24, // cleared on DMA completion
				StartTrigger = 1u << 28, // cleared on DMA begin
				Pause = 1u << 29,
				Unknown = 1u << 30,

				WriteMask = TransferDirection | MemoryAddressStep | ChoppingEnable | SyncModeMask | ChoppingDmaWindowSize |
							ChoppingCpuWindowSize | StartBusy | StartTrigger | Pause | Unknown,
			};
		};

		enum class TransferDirection : uint32_t
		{
			ToMainRam,
			FromMainRam
		};

		enum class MemoryAddressStep : uint32_t
		{
			Forward,
			Backward
		};

		enum class SyncMode : uint32_t
		{
			Manual,
			Request,
			LinkedList,
			Unused
		};

	public:
		void Reset();

		uint32_t Read( uint32_t index ) const noexcept;

		void Write( uint32_t index, uint32_t value ) noexcept;

		bool Active() const noexcept
		{
			return GetStartBusy() && ( GetSyncMode() == SyncMode::Manual ? GetStartTrigger() : true );
		}

		uint32_t GetWordCount() const noexcept;

		// base address register

		uint32_t GetBaseAddress() const noexcept { return m_baseAddress; }

		// block control register

		uint32_t GetBlockSize() const noexcept
		{
			const uint32_t blockSize = m_blockControl & 0x0000ffff;
			return blockSize != 0 ? blockSize : 0x00010000;
		}

		uint32_t GetBlockCount() const noexcept { return static_cast<uint16_t>( m_blockControl >> 16 ); }

		// control register

		TransferDirection GetTransferDirection() const noexcept
		{
			return static_cast<TransferDirection>( m_channelControl & ChannelControl::TransferDirection );
		}

		MemoryAddressStep GetMemoryAddressStep() const noexcept
		{
			return static_cast<MemoryAddressStep>( ( m_channelControl & ChannelControl::MemoryAddressStep ) >> 1 );
		}

		bool GetChoppingEnable() const noexcept { return m_channelControl & ChannelControl::ChoppingEnable; }

		SyncMode GetSyncMode() const noexcept
		{
			return static_cast<SyncMode>( ( m_channelControl & ChannelControl::SyncModeMask ) >> 9 );
		}

		uint32_t GetChoppingDmaWindowSize() const noexcept
		{
			return ( m_channelControl & ChannelControl::ChoppingDmaWindowSize ) >> 16;
		}

		uint32_t GetChoppingCpuWindowSize() const noexcept
		{
			return ( m_channelControl & ChannelControl::ChoppingCpuWindowSize ) >> 20;
		}

		bool GetStartBusy() const noexcept { return m_channelControl & ChannelControl::StartBusy; }

		bool GetStartTrigger() const noexcept { return m_channelControl & ChannelControl::StartTrigger; }

		void SetTransferComplete() noexcept
		{
			m_channelControl &= ~( ChannelControl::StartBusy | ChannelControl::StartTrigger );
		}

	private:
		uint32_t m_baseAddress;
		uint32_t m_blockControl;
		uint32_t m_channelControl;
	};

	static constexpr uint32_t ControlRegisterResetValue = 0x07654321;

	enum InterruptRegister : uint32_t
	{
		UnknownMask = 0x3fu,
		ForceIrq = 1u << 15,
		IrqEnablesMask = 0x7fu << 16,
		IrqMasterEnable = 1u << 23,
		IrqFlagsMask = 0x7fu << 24, // write 1 to reset
		IrqMasterFlag = 1u << 31, // read only

		WriteMask = UnknownMask | ForceIrq | IrqEnablesMask | IrqMasterEnable,
	};

public:
	Dma( Ram& ram, Gpu& gpu, InterruptControl& interruptControl )
		: m_ram{ ram }, m_gpu{ gpu }, m_interruptControl{ interruptControl }
	{
		Reset();
	}

	void Reset();

	uint32_t Read( uint32_t index ) const noexcept;

	void Write( uint32_t index, uint32_t value ) noexcept;

	void DoBlockTransfer( uint32_t channel ) noexcept;

	void DoLinkedListTransfer( uint32_t channel ) noexcept;

	// control register

	uint32_t GetChannelPriority( uint32_t channel ) const noexcept
	{
		dbExpects( channel < 7 );
		return ( m_controlRegister >> ( channel * 4 ) ) & 0x03;
	}

	bool GetChannelMasterEnable( uint32_t channel ) const noexcept
	{
		dbExpects( channel < 7 );
		return m_controlRegister & ( 1 << ( 3 + channel * 4 ) );
	}

	// interrupt register

private:
	void FinishTransfer( uint32_t channelIndex ) noexcept;

	bool GetForceIrq() const noexcept { return m_interruptRegister & InterruptRegister::ForceIrq; }

	uint32_t GetIrqEnables() const noexcept { return ( m_interruptRegister >> 16 ) & 0x0000007f; }

	bool GetIrqMasterEnable() const noexcept { return m_interruptRegister & InterruptRegister::IrqMasterEnable; }

	uint32_t GetIrqFlags() const noexcept { return ( m_interruptRegister >> 24 ) & 0x0000007f; }

	bool GetIrqMasterFlag() const noexcept
	{
		return GetForceIrq() || ( GetIrqMasterEnable() && ( GetIrqEnables() & GetIrqFlags() ) != 0 );
	}

private:
	static constexpr uint32_t LinkedListTerminator = 0x00ffffff;

	Ram& m_ram;
	Gpu& m_gpu;
	InterruptControl& m_interruptControl;

	std::array<Channel, 7> m_channels;

	uint32_t m_controlRegister;
	uint32_t m_interruptRegister;
};

}