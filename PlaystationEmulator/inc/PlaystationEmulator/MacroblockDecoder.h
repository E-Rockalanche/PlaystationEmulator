#pragma once

#include "Defs.h"
#include "FifoBuffer.h"

#include <stdx/bit.h>

namespace PSX
{

class MacroblockDecoder
{
public:
	MacroblockDecoder() {}

	void Reset();

	uint32_t Read( uint32_t offset );

	void Write( uint32_t offset, uint32_t value );

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
		Invalid,
		DecodeMacroblock,
		SetQuantTable,
		SetScaleTable
	};

	enum class State
	{
		Idle,
		ReadingMacroblock,
		DecodingMacroblock,
		ReadingQuantTable,
		ReadingScaleTable,
		InvalidCommand
	};

	enum class BlockIndex
	{
		Y1,
		Y2,
		Y3,
		Y4,
		Cr,
		Cb,
		Y = 4 // for monochrome
	};

	using Block = std::array<int16_t, 64>;

	static constexpr uint16_t EndOfData = 0xfe00;

private:
	uint32_t ReadData();
	uint32_t ReadStatus();

	void WriteParam( uint32_t value );
	void StartCommand( uint32_t value );

	void Decode();

	// decompression functions

	bool rl_decode_block( int16_t* blk, const uint8_t* qt ); // returns true when the block is full
	void real_idct_core( Block& blk );
	void yuv_to_rgb( size_t xx, size_t yy, const Block& crBlk, const Block& cbBlk, const Block& yBlk );
	void y_to_mono( const Block& yBlk );

private:
	uint32_t m_remainingHalfWords = 0;
	BlockIndex m_readBlock{};
	BlockIndex m_writeBlock{};

	bool m_dataOutputBit15 = false;
	bool m_dataOutputSigned = false;

	DataOutputDepth m_dataOutputDepth{};

	bool m_enableDataOut = false;
	bool m_enableDataIn = false;

	bool m_color = false;

	State m_state{};

	FifoBuffer<uint16_t, 512> m_dataInBuffer;
	FifoBuffer<uint32_t, 192> m_dataOutBuffer;

	std::array<uint8_t, 64> m_luminanceTable{}; // used for Y1-Y4
	std::array<uint8_t, 64> m_colorTable{}; // used for Cr and Cb

	Block m_scaleTable{}; // should be the same as the standard JPEG constants

	size_t m_currentK = 0;
	int16_t m_currentQ = 0;

	std::array<uint32_t, 64> m_colorDest{};
};

}