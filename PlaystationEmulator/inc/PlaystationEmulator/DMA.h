#pragma once

#include "Defs.h"

#include "RAM.h"

#include <stdx/assert.h>
#include <stdx/bit.h>

#include <array>

namespace PSX
{

class Dma
{
public:
	enum class Channel
	{
		MDecIn,
		MDecOut,
		Gpu,
		CdRom,
		Spu,
		ExtensionPort,
		RamOrderTable
	};

public:
	Dma( Ram& ram,
		Gpu& gpu,
		CDRomDrive& cdromDRive,
		MacroblockDecoder& mdec,
		InterruptControl& interruptControl,
		EventManager& eventManager )
		: m_ram{ ram }
		, m_gpu{ gpu }
		, m_cdromDrive{ cdromDRive }
		, m_mdec{ mdec }
		, m_interruptControl{ interruptControl }
		, m_eventManager{ eventManager }
	{}

	void Reset();

	uint32_t Read( uint32_t index ) const noexcept;

	void Write( uint32_t index, uint32_t value ) noexcept;

	void SetRequest( Channel channel, bool request ) noexcept;

private:
	enum class ChannelRegister
	{
		BaseAddress,
		BlockControl,
		ChannelControl
	};

	enum class SyncMode : uint32_t
	{
		Manual,
		Request,
		LinkedList,
		Unused
	};

	struct ChannelState
	{
		uint32_t GetWordCount() const noexcept { return wordCount != 0 ? wordCount : 0x00010000; }
		uint32_t GetBlockSize() const noexcept { return GetWordCount(); }
		uint32_t GetBlockCount() const noexcept { return blockCount != 0 ? blockCount : 0x00010000; }

		SyncMode GetSyncMode() const noexcept { return static_cast<SyncMode>( control.syncMode ); }

		uint32_t GetChoppingDmaWindowSize() const noexcept { return 1 << control.choppingDmaWindowSize; }
		uint32_t GetChoppingCpuWindowSize() const noexcept { return 1 << control.choppingCpuWindowSize; }

		void SetBaseAddress( uint32_t value ) noexcept { baseAddress = value & BaseAddressMask; }

		union Control
		{
			struct
			{
				uint32_t transferDirection : 1; // 0=to ram, 1=from ram
				uint32_t memoryAddressStep : 1; // 0=4, 1=-4
				uint32_t : 6;

				uint32_t choppingEnable : 1;
				uint32_t syncMode : 2; // 0=manual, 1=request, 2=linked list
				uint32_t : 5;

				uint32_t choppingDmaWindowSize : 3; // 1 << N
				uint32_t : 1;
				uint32_t choppingCpuWindowSize : 3; // 1 << N
				uint32_t : 1;

				uint32_t startBusy : 1; // cleared on dma completion
				uint32_t : 3;
				uint32_t startTrigger : 1; // cleared on dma begin
				uint32_t pause : 1;
				uint32_t unknown : 1;
				uint32_t : 1;
			};
			uint32_t value = 0;

			static constexpr uint32_t WriteMask = 0x71770703;
		};
		static_assert( sizeof( Control ) == 4 );

		static constexpr uint32_t BaseAddressMask = 0x00ffffffu;

		uint32_t baseAddress = 0;
		uint16_t wordCount = 0; // also block size
		uint16_t blockCount = 0;
		Control control;
		bool request = false;
	};

	union InterruptRegister
	{
		InterruptRegister() : value{ 0 } {}

		void UpdateIrqMasterFlag() noexcept
		{
			irqMasterFlag = forceIrq || ( irqMasterEnable && ( ( irqEnables & irqFlags ) != 0 ) );
		}

		struct
		{
			uint32_t unknown : 6;
			uint32_t : 9;
			uint32_t forceIrq : 1; // 1=set irqMasterFlag
			uint32_t irqEnables : 7;
			uint32_t irqMasterEnable : 1;
			uint32_t irqFlags : 7; // write 1 to reset
			uint32_t irqMasterFlag : 1; // read only
		};
		uint32_t value;

		static constexpr uint32_t IrqFlagsMask = 0x7f000000;
		static constexpr uint32_t WriteMask = 0x00ff803f;
	};
	static_assert( sizeof( InterruptRegister ) == 4 );

	enum class Register
	{
		// DMA0-DMA6
		Control = ( 0x1F8010F0 - 0x1F801080 ) / 4,
		Interrupt,
		Unknown1,
		Unknown2,
	};

	static constexpr uint32_t ControlRegisterResetValue = 0x07654321;
	static constexpr uint32_t LinkedListTerminator = 0x00ffffff;
	static constexpr uint32_t DmaAddressMask = RamAddressMask & ChannelState::BaseAddressMask;
	static constexpr uint32_t ForwardStep = 4;
	static constexpr uint32_t BackwardStep = uint32_t( -4 );

private:

	uint32_t GetChannelPriority( Channel channel ) const noexcept
	{
		return ( m_controlRegister >> ( static_cast<uint32_t>( channel ) * 4 ) ) & 0x03u;
	}

	bool IsChannelEnabled( Channel channel ) const noexcept
	{
		return stdx::any_of<uint32_t>( m_controlRegister, 0x8 << ( static_cast<uint32_t>( channel ) * 4 ) );
	}

	bool CanTransferChannel( Channel channel ) const noexcept;

	void StartDma( Channel channel );

	void TransferToRam( Channel channel, uint32_t address, uint32_t words, uint32_t addressStep );
	void TransferFromRam( Channel channel, uint32_t address, uint32_t words, uint32_t addressStep );

	void ClearOrderTable( uint32_t address, uint32_t wordCount );

	void FinishTransfer( Channel channel ) noexcept;

	static uint32_t GetCyclesForTransfer( uint32_t words ) noexcept
	{
		return words + ( words * 0x10 ) / 0x100;
	}

	void ResizeTempBuffer( uint32_t newSize )
	{
		if ( newSize > m_tempBufferSize )
			m_tempBuffer = std::make_unique<uint32_t[]>( newSize );
	}

private:
	Ram& m_ram;
	Gpu& m_gpu;
	CDRomDrive& m_cdromDrive;
	MacroblockDecoder& m_mdec;
	InterruptControl& m_interruptControl;
	EventManager& m_eventManager;

	std::array<ChannelState, 7> m_channels;

	uint32_t m_controlRegister = 0;
	InterruptRegister m_interruptRegister;

	std::unique_ptr<uint32_t[]> m_tempBuffer;
	uint32_t m_tempBufferSize = 0;
};

}