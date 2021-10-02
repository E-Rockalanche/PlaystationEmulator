#pragma once

#include "Defs.h"

#include "RAM.h"

#include <stdx/assert.h>

#include <array>

namespace PSX
{

class Dma
{
public:
	enum class ChannelIndex
	{
		MDecIn,
		MDecOut,
		Gpu,
		CdRom,
		Spu,
		ExtensionPort,
		RamOrderTable
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

		union Control
		{
			Control() : value{ 0 } {}

			struct
			{
				uint32_t transferDirection : 1;
				uint32_t memoryAddressStep : 1;
				uint32_t : 6;
				uint32_t choppingEnable : 1;
				uint32_t syncMode : 2;
				uint32_t : 5;
				uint32_t choppingDmaWindowSize : 3;
				uint32_t : 1;
				uint32_t choppingCpuWindowSize : 3;
				uint32_t : 1;
				uint32_t startBusy : 1; // cleared on dma completion
				uint32_t : 3;
				uint32_t startTrigger : 1; // cleared on dma begin
				uint32_t pause : 1;
				uint32_t : 2;
			};
			uint32_t value;

			static constexpr uint32_t WriteMask = 0x31770702;
		};
		static_assert( sizeof( Control ) == 4 );

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

		void SetBaseAddress( uint32_t address )
		{
			m_baseAddress = address & BaseAddressMask;
		}

		// block control register

		uint32_t GetBlockSize() const noexcept
		{
			return ( m_blockSize != 0 ) ? static_cast<uint32_t>( m_blockSize ) : 0x00010000u;
		}

		uint32_t GetBlockCount() const noexcept { return m_blockCount; }

		// control register

		TransferDirection GetTransferDirection() const noexcept
		{
			return static_cast<TransferDirection>( m_control.transferDirection );
		}

		MemoryAddressStep GetMemoryAddressStep() const noexcept
		{
			return static_cast<MemoryAddressStep>( m_control.memoryAddressStep );
		}

		bool GetChoppingEnable() const noexcept
		{
			return m_control.choppingEnable;
		}

		SyncMode GetSyncMode() const noexcept
		{
			return static_cast<SyncMode>( m_control.syncMode );
		}

		uint32_t GetChoppingDmaWindowSize() const noexcept
		{
			return m_control.choppingDmaWindowSize;
		}

		uint32_t GetChoppingCpuWindowSize() const noexcept
		{
			return m_control.choppingCpuWindowSize;
		}

		bool GetStartBusy() const noexcept { return m_control.startBusy; }

		bool GetStartTrigger() const noexcept { return m_control.startTrigger; }

		void SetTransferComplete() noexcept
		{
			m_control.startBusy = false;
			m_control.startTrigger = false;
		}

	private:
		uint32_t m_baseAddress = 0;
		uint16_t m_blockSize = 0;
		uint16_t m_blockCount = 0;
		Control m_control;
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
	Dma( Ram& ram, Gpu& gpu, CDRomDrive& cdromDRive, InterruptControl& interruptControl, EventManager& eventManager )
		: m_ram{ ram }
		, m_gpu{ gpu }
		, m_cdromDrive{ cdromDRive }
		, m_interruptControl{ interruptControl }
		, m_eventManager{ eventManager }
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
		return ( m_controlRegister >> ( channel * 4 ) ) & 0x03u;
	}

	bool GetChannelMasterEnable( uint32_t channel ) const noexcept
	{
		dbExpects( channel < 7 );
		return m_controlRegister & ( 1u << ( 3 + channel * 4 ) );
	}

	// interrupt register

private:
	static constexpr uint32_t LinkedListTerminator = 0x00ffffff;

	enum class Register
	{
		// DMA0-DMA6
		Control = ( 0x1F8010F0 - 0x1F801080 ) / 4,
		Interrupt,
		Unknown1,
		Unknown2,
	};

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

	static uint32_t GetCyclesForTransfer( ChannelIndex channel, uint32_t words ) noexcept;

private:
	Ram& m_ram;
	Gpu& m_gpu;
	CDRomDrive& m_cdromDrive;
	InterruptControl& m_interruptControl;
	EventManager& m_eventManager;

	std::array<Channel, 7> m_channels;

	uint32_t m_controlRegister = 0;
	uint32_t m_interruptRegister = 0;
};

}