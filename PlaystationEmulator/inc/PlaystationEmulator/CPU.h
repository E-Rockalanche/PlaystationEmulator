#pragma once

#include "Instruction.h"
#include "MemoryMap.h"

#include "assert.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>

namespace PSX
{

template <typename T>
static inline constexpr uint32_t Extend( T value ) noexcept
{
	using ExtendedType = std::conditional_t<std::is_signed_v<T>, int32_t, uint32_t>;
	return static_cast<uint32_t>( static_cast<ExtendedType>( value ) );
}

class MipsR3000Cpu
{
public:

	static constexpr size_t BiosSize = 512 * 1024;

	MipsR3000Cpu( MemoryMap& memoryMap );

	void Reset();

	void Tick() noexcept;

private:

	struct MultiplyUnit
	{
		void MultiplySigned( uint32_t x, uint32_t y )
		{
			result = static_cast<uint64_t>(
				static_cast<int64_t>( static_cast<int32_t>( x ) ) *
				static_cast<int64_t>( static_cast<int32_t>( y ) ) );
		}

		void MultiplyUnsigned( uint32_t x, uint32_t y )
		{
			result = static_cast<uint64_t>( static_cast<uint64_t>( x ) * static_cast<uint64_t>( y ) );
		}

		void DivideSigned( uint32_t x, uint32_t y )
		{
			const auto divresult = std::div( static_cast<int32_t>( x ), static_cast<int32_t>( y ) );
			registers.lo = static_cast<uint32_t>( divresult.quot );
			registers.hi = static_cast<uint32_t>( divresult.rem );
		}

		void DivideUnsigned( uint32_t x, uint32_t y )
		{
			const auto divresult = std::div( static_cast<int64_t>( x ), static_cast<int64_t>( y ) );
			registers.lo = static_cast<uint32_t>( divresult.quot );
			registers.hi = static_cast<uint32_t>( divresult.rem );
		}

		union
		{
			uint64_t result;
			struct
			{
				uint32_t lo;
				uint32_t hi;
			} registers;
		};
	};

	class Registers
	{
	public:
		static constexpr uint32_t Zero = 0;
		static constexpr uint32_t Assembler = 1;
		// 2-3 value returned by subroutine
		// 4-7 first 4 arguments of subroutine
		// 8-15, 24-25 subroutine temporaries
		// 16-23 subroutine register variables (preserved)
		static constexpr uint32_t Trap0 = 26;
		static constexpr uint32_t Trap1 = 27;
		static constexpr uint32_t GlobalPointer = 28;
		static constexpr uint32_t StackPointer = 29;
		static constexpr uint32_t FramePointer = 30;
		static constexpr uint32_t ReturnAddress = 31;

		uint32_t operator[]( uint32_t index ) const noexcept { return m_input[ index ]; }

		// immediately updates register
		void Set( uint32_t index, uint32_t value ) noexcept
		{
			// update input early so we can overwrite any delayed load
			m_input[ m_output.index ] = m_output.value;
			m_input[ Zero ] = 0;

			m_output = { index, value };
		}

		// emulates delayed load
		void Load( uint32_t index, uint32_t value ) noexcept
		{
			dbExpects( m_delayedLoad.index == 0 && m_delayedLoad.value == 0 );
			m_delayedLoad = { index, value };
		}

		void Reset() noexcept
		{
			m_input.fill( 0 );
			m_output = { 0, 0 };
			m_delayedLoad = { 0, 0 };
		}

		void Update() noexcept
		{
			m_input[ m_output.index ] = m_output.value;
			m_input[ Zero ] = 0; // zero register is always 0
			m_output = m_delayedLoad;
			m_delayedLoad = { 0, 0 };
		}

		// used by LWL and LWR to emulate special hardware that allows for both instructions to be used without a NOP inbetween
		uint32_t GetOutputIndex() const noexcept { return m_output.index; }
		uint32_t GetOutputValue() const noexcept { return m_output.value; }

	private:
		struct DelayedLoad
		{
			uint32_t index = 0;
			uint32_t value = 0;
		};

	private:
		std::array<uint32_t, 32> m_input; // current register values
		DelayedLoad m_output; // new register value to update after instruction
		DelayedLoad m_delayedLoad; // delayed load which will update register after next instruction
	};

	void AddTrap( uint32_t x, uint32_t y, uint32_t destRegister ) noexcept;

	void SubtractTrap( uint32_t x, uint32_t y, uint32_t destRegister ) noexcept;

	void BranchImp( bool condition, int16_t offset ) noexcept;

	void JumpImp( uint32_t target ) noexcept;

	void CheckProgramCounterAlignment() noexcept;

private: // READ / WRITE

	uint32_t GetVAddr( Instruction instr ) const noexcept
	{
		return m_registers[ instr.base() ] + Extend( instr.immediate() );
	}

	template <typename T>
	uint32_t LoadImp( Instruction instr )
	{
		return Extend( m_memoryMap.Read<T>( GetVAddr( instr ) ) );
	}

	template <typename T>
	void StoreImp( uint32_t base, int16_t offset, T value )
	{
		const uint32_t virtualAddress = m_registers[ base ] + Extend( offset );
		m_memoryMap.Write<T>( virtualAddress, value );
	}

private: // instructions

	// SPECIAL
	void Special( Instruction ) noexcept;

	// REGIMM
	void RegisterImmediate( Instruction ) noexcept;

	// COPz
	void CoprocessorUnit( Instruction ) noexcept;

	// ADD
	void Add( Instruction ) noexcept;

	// ADDI
	void AddImmediate( Instruction ) noexcept;

	// ADDIU
	void AddImmediateUnsigned( Instruction ) noexcept;

	// ADDU
	void AddUnsigned( Instruction ) noexcept;

	// AND
	void BitwiseAnd( Instruction ) noexcept;

	// AND
	void AndImmediate( Instruction ) noexcept;

	// BEQ
	void BranchEqual( Instruction ) noexcept;

	// BGEZ
	void BranchGreaterEqualZero( Instruction ) noexcept;

	// BGEZAL
	void BranchGreaterEqualZeroAndLink( Instruction ) noexcept;

	// BGTZ
	void BranchGreaterThanZero( Instruction ) noexcept;

	// BLEZ
	void BranchLessEqualZero( Instruction ) noexcept;

	// BLTZ
	void BranchLessThanZero( Instruction ) noexcept;

	// BLTZAL
	void BranchLessThanZeroAndLink( Instruction ) noexcept;

	// BNE
	void BranchNotEqual( Instruction ) noexcept;

	// BREAK
	void Break( Instruction ) noexcept;

	// CFCz
	void MoveControlFromCoprocessor( Instruction ) noexcept;

	// COPz
	void CoprocessorOperation( Instruction ) noexcept;

	// CTCz
	void MoveControlToCoprocessor( Instruction ) noexcept;

	// DIV
	void Divide( Instruction ) noexcept;

	// DIVU
	void DivideUnsigned( Instruction ) noexcept;

	// J
	void Jump( Instruction ) noexcept;

	// JAL
	void JumpAndLink( Instruction ) noexcept;

	// JALR
	void JumpAndLinkRegister( Instruction ) noexcept;

	// JR
	void JumpRegister( Instruction ) noexcept;

	// LB
	void LoadByte( Instruction ) noexcept;

	// LBU
	void LoadByteUnsigned( Instruction ) noexcept;

	// LH
	void LoadHalfword( Instruction ) noexcept;

	// LHU
	void LoadHalfwordUnsigned( Instruction ) noexcept;

	// LUI
	void LoadUpperImmediate( Instruction ) noexcept;

	// LW
	void LoadWord( Instruction ) noexcept;

	// LWCz
	void LoadWordToCoprocessor( Instruction ) noexcept;

	// LWL
	void LoadWordLeft( Instruction ) noexcept;

	// LWR
	void LoadWordRight( Instruction ) noexcept;

	// MFCz
	void MoveFromCoprocessor( Instruction ) noexcept;

	// MFHI
	void MoveFromHi( Instruction ) noexcept;

	// MFLO
	void MoveFromLo( Instruction ) noexcept;

	// MTCz
	void MoveToCoprocessor( Instruction ) noexcept;

	// MTHI
	void MoveToHi( Instruction ) noexcept;

	// MTLO
	void MoveToLo( Instruction ) noexcept;

	// MULT
	void Multiply( Instruction ) noexcept;

	// MULTU
	void MultiplyUnsigned( Instruction ) noexcept;

	// NOR
	void BitwiseNor( Instruction ) noexcept;

	// OR
	void BitwiseOr( Instruction ) noexcept;

	// ORI
	void BitwiseOrImmediate( Instruction ) noexcept;

	// SB
	void StoreByte( Instruction ) noexcept;

	// SH
	void StoreHalfword( Instruction ) noexcept;

	// SLL
	void ShiftLeftLogical( Instruction ) noexcept;

	// SLLV
	void ShiftLeftLogicalVariable( Instruction ) noexcept;

	// SLT
	void SetLessThan( Instruction ) noexcept;

	// SLTI
	void SetLessThanImmediate( Instruction ) noexcept;

	// SLTIU
	void SetLessThanImmediateUnsigned( Instruction ) noexcept;

	// SLTU
	void SetLessThanUnsigned( Instruction ) noexcept;

	// SRA
	void ShiftRightArithmetic( Instruction ) noexcept;

	// SRAV
	void ShiftRightArithmeticVariable( Instruction ) noexcept;

	// SRL
	void ShiftRightLogical( Instruction ) noexcept;

	// SRLV
	void ShiftRightLogicalVariable( Instruction ) noexcept;

	// SUB
	void Subtract( Instruction ) noexcept;

	// SUBU
	void SubtractUnsigned( Instruction ) noexcept;

	// SW
	void StoreWord( Instruction ) noexcept;

	// SWCz
	void StoreWordFromCoprocessor( Instruction ) noexcept;

	// SWL
	void StoreWordLeft( Instruction ) noexcept;

	// SWR
	void StoreWordRight( Instruction ) noexcept;

	// SYSCALL
	void SystemCall( Instruction ) noexcept;

	// XOR
	void BitwiseXor( Instruction ) noexcept;

	// XORI
	void BitwiseXorImmediate( Instruction ) noexcept;

	void UnhandledInstruction( Instruction ) noexcept;

private:
	MemoryMap& m_memoryMap;

	uint32_t m_pc;

	Instruction m_nextInstruction;

	Registers m_registers;

	MultiplyUnit m_multiplyUnit;

	using InstructionFunction = void( MipsR3000Cpu::* )( Instruction ) noexcept;

	std::array<InstructionFunction, 64> m_instructions{};
	std::array<InstructionFunction, 64> m_specialInstructions{};

	std::array<const char*, 64> m_instructionNames{};
	std::array<const char*, 64> m_specialInstructionNames{};
};

}