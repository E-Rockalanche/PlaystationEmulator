#pragma once

#include "Defs.h"

#include "RAM.h"

#include <stdx/assert.h>
#include <stdx/bit.h>

#include <array>
#include <vector>

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
	static constexpr size_t ChannelCount = 7;

	enum class DmaResult
	{
		Chopping,
		WaitRequest,
		Finished
	};

public:
	Dma( Ram& ram,
		Gpu& gpu,
		CDRomDrive& cdromDRive,
		MacroblockDecoder& mdec,
		Spu& spu,
		InterruptControl& interruptControl,
		EventManager& eventManager );

	void Reset();

	uint32_t Read( uint32_t index ) const noexcept;

	void Write( uint32_t index, uint32_t value ) noexcept;

	void SetRequest( Channel channel, bool request ) noexcept;

	void Serialize( SaveStateSerializer& serializer );

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

		uint32_t GetChoppingDmaWindowSize() const noexcept { return static_cast<uint32_t>( 1 << control.choppingDmaWindowSize ); }
		uint32_t GetChoppingCpuWindowSize() const noexcept { return static_cast<uint32_t>( 1 << control.choppingCpuWindowSize ); }

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
		uint32_t value = 0;

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
	static constexpr uint32_t DmaAddressMask = RamAddressMask & 0xfffffffc;
	static constexpr uint32_t ForwardStep = 4;
	static constexpr uint32_t BackwardStep = uint32_t( -4 );

private:

	uint32_t GetChannelPriority( Channel channel ) const noexcept
	{
		return ( m_controlRegister >> ( static_cast<uint32_t>( channel ) * 4 ) ) & 0x07u;
	}

	bool IsChannelEnabled( Channel channel ) const noexcept
	{
		return stdx::any_of<uint32_t>( m_controlRegister, 0x8 << ( static_cast<uint32_t>( channel ) * 4 ) );
	}

	bool CanTransferChannel( Channel channel ) const noexcept;

	DmaResult StartDma( Channel channel );

	void TransferToRam( Channel channel, uint32_t address, uint32_t words, uint32_t addressStep );
	void TransferFromRam( Channel channel, uint32_t address, uint32_t words, uint32_t addressStep );

	void ClearOrderTable( uint32_t address, uint32_t wordCount );

	void FinishTransfer( Channel channel ) noexcept;

	static constexpr bool NeedsTempBuffer( uint32_t address, uint32_t wordCount, uint32_t addressStep )
	{
		return ( addressStep == BackwardStep ) || ( ( address & DmaAddressMask ) + wordCount * 4 > RamSize );
	}

	// DMA is using DRAM Hyper Page mode, allowing it to access DRAM rows at 1 clock cycle per word
	// (effectively around 17 clks per 16 words, due to required row address loading, probably plus some further minimal overload due to refresh cycles).
	// This is making DMA much faster than CPU memory accesses (CPU DRAM access takes 1 opcode cycle plus 6 waitstates, ie. 7 cycles in total)
	static constexpr cycles_t GetCyclesForWords( uint32_t words ) noexcept
	{
		return static_cast<cycles_t>( ( words * 17 + 15 ) / 16 );
	}

	static constexpr uint32_t GetWordsForCycles( cycles_t cycles ) noexcept
	{
		return static_cast<uint32_t>( ( cycles * 16 + 16 ) / 17 );
	}

	void ResumeDma();

private:
	Ram& m_ram;
	Gpu& m_gpu;
	CDRomDrive& m_cdromDrive;
	MacroblockDecoder& m_mdec;
	Spu& m_spu;
	InterruptControl& m_interruptControl;
	EventManager& m_eventManager;

	EventHandle m_resumeDmaEvent;

	std::array<ChannelState, ChannelCount> m_channels;

	uint32_t m_controlRegister = 0;
	InterruptRegister m_interruptRegister;

	// not serialized
	std::vector<uint32_t> m_tempBuffer;
};

}