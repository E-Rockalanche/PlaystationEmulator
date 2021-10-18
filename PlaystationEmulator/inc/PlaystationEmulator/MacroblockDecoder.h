#pragma once

#include "Defs.h"
#include "FifoBuffer.h"

#include <stdx/bit.h>

namespace PSX
{

class MacroblockDecoder
{
public:
	MacroblockDecoder( EventManager& eventManager );

	void SetDma( Dma& dma ) { m_dma = &dma; }

	void Reset();

	uint32_t Read( uint32_t offset )
	{
		dbExpects( offset < 2 );
		if ( offset == 0 )
			return ReadData();
		else
			return m_status.value;
	}

	void Write( uint32_t offset, uint32_t value );

	void DmaIn( const uint32_t* input, uint32_t count );
	void DmaOut( uint32_t* output, uint32_t count );

private:
	union Status
	{
		Status() noexcept : value{ 0 } {}

		struct
		{
			uint32_t remainingParameters : 16;	
			uint32_t currentBlock : 3;			// (0..3=Y1..Y4, 4=Cr, 5=Cb) (or for mono: always 4=Y)
			uint32_t : 4;
			uint32_t dataOutputBit15 : 1;		// (0=Clear, 1=Set) (for 15bit depth only)
			uint32_t dataOutputSigned : 1;		// (0=Unsigned, 1=Signed)
			uint32_t dataOutputDepth : 2;		// (0=4bit, 1=8bit, 2=24bit, 3=15bit)
			uint32_t dataOutRequest : 1;		// (set when DMA1 enabled and ready to send data)
			uint32_t dataInRequest : 1;			// (set when DMA0 enabled and ready to receive data)
			uint32_t commandBusy : 1;			// (0=Ready, 1=Busy receiving or processing parameters)
			uint32_t dataInFifoFull : 1;
			uint32_t dataOutFifoEmpty : 1;
		};
		uint32_t value;
	};
	static_assert( sizeof( Status ) == 4 );

	enum class DataOutputDepth : uint32_t
	{
		Four,
		Eight,
		TwentyFour,
		Fifteen
	};

	enum class Command
	{
		DecodeMacroblock = 1,
		SetQuantTable = 2,
		SetScaleTable = 3
	};

	enum class State
	{
		Idle,
		DecodingMacroblock,
		WritingMacroblock,
		ReadingQuantTable,
		ReadingScaleTable,
		InvalidCommand
	};

	// not the same order as block index in status register!
	struct BlockIndex
	{
		enum : uint32_t
		{
			Cr,
			Cb,
			Y1,
			Y2,
			Y3,
			Y4,
			Count,
			Y = Cr
		};
	};

	using Block = std::array<int16_t, 64>;
	using Table = std::array<uint8_t, 64>;

	static constexpr uint16_t EndOfBlock = 0xfe00;

private:
	uint32_t ReadData();
	void UpdateStatus();

	void ProcessInput();

	void StartCommand( uint32_t value );

	bool DecodeMacroblock()
	{
		if ( m_status.dataOutputDepth < 2 )
			return DecodeMonoMacroblock(); // 4bit & 8bit
		else
			return DecodeColoredMacroblock(); // 24bit & 15bit
	}

	bool DecodeColoredMacroblock(); // returns true when data is ready to be output
	bool DecodeMonoMacroblock(); // returns true when data is ready to be output

	void ScheduleOutput();
	void OutputBlock();

	// decompression functions

	bool rl_decode_block( Block& blk, const Table& qt ); // returns true when the block is full
	void real_idct_core( Block& blk );
	void yuv_to_rgb( size_t xx, size_t yy, const Block& crBlk, const Block& cbBlk, const Block& yBlk );
	void y_to_mono( const Block& yBlk );

private:
	EventHandle m_outputBlockEvent;
	Dma* m_dma = nullptr;

	Status m_status;

	uint32_t m_remainingHalfWords = 0;

	bool m_enableDataOut = false;
	bool m_enableDataIn = false;

	bool m_color = false;

	State m_state{};

	FifoBuffer<uint16_t, 512> m_dataInBuffer; // unsure of max size
	FifoBuffer<uint32_t, ( 16 * 16 * 3 ) / 4> m_dataOutBuffer; // at most 16x16 24bit packed pixels

	std::array<uint8_t, 64> m_luminanceTable{}; // used for Y1-Y4
	std::array<uint8_t, 64> m_colorTable{}; // used for Cr and Cb

	Block m_scaleTable{}; // should be the same as the standard JPEG constants

	size_t m_currentK = 0;
	int16_t m_currentQ = 0;

	std::array<Block, BlockIndex::Count> m_blocks;
	uint32_t m_currentBlock = 0;

	std::array<uint32_t, 256> m_dest;
};

}