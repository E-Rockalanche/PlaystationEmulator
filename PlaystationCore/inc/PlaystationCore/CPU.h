#pragma once

#include "Defs.h"

#include "Cop0.h"
#include "GTE.h"
#include "Instruction.h"
#include "MemoryMap.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace PSX
{

class MipsR3000Cpu
{
public:

	bool EnableKernelLogging = false;
	bool EnableCpuLogging = false;
	bool EnableBiosIntercept = true;

	MipsR3000Cpu( MemoryMap& memoryMap, InterruptControl& interruptControl, EventManager& eventManager )
		: m_memoryMap{ memoryMap }
		, m_interruptControl{ interruptControl }
		, m_eventManager{ eventManager }
		, m_cop0{ interruptControl }
	{}

	void Reset();

	void RunUntilEvent() noexcept;

	void DebugSetProgramCounter( uint32_t address )
	{
		SetProgramCounter( address );
		m_inBranch = false;
		m_inDelaySlot = false;
	}

	void DebugSetRegister( uint32_t reg, uint32_t value )
	{
		dbExpects( reg < 32 );
		m_registers.Set( reg, value );
		m_registers.Update();
	}

	uint32_t GetPC() const noexcept { return m_pc; }

	void SetHookExecutable( fs::path filename )
	{
		m_exeFilename = std::move( filename );
	}

	void Serialize( SaveStateSerializer& serializer );

private:

	static constexpr uint32_t ResetVector = 0xbfc00000;
	static constexpr uint32_t DebugBreakVector = 0x80000040; // COP0 break
	static constexpr uint32_t InterruptVector = 0x80000080; // used for general interrupts and exceptions
	static constexpr uint32_t HookAddress = 0x80030000; // address at which an exe can be loaded

	class Registers
	{
	public:
		enum : uint32_t
		{
			Zero,
			AssemblerTemp,
			Retval0, Retval1,
			Arg0, Arg1, Arg2, Arg3,
			Temp0, Temp1, Temp2, Temp3, Temp4, Temp5, Temp6, Temp7,
			Static0, Static1, Static2, Static3, Static4, Static5, Static6, Static7,
			Temp8, Temp9,
			Kernel0, Kernel1,
			GlobalPointer,
			StackPointer,
			FramePointer,
			ReturnAddress,

			Static8 = FramePointer,
		};

		inline uint32_t operator[]( uint32_t index ) const noexcept
		{
			return m_registers[ index ];
		}

		// immediately updates register
		inline void Set( uint32_t index, uint32_t value ) noexcept
		{
			dbExpects( index < 32 );

			m_registers[ index ] = value;
			m_registers[ Zero ] = 0;

			// overwrite load delay if for the same register
			if ( m_loadDelay.index == index )
				m_loadDelay.index = 0;
		}

		// emulates delayed load
		inline void Load( uint32_t index, uint32_t value ) noexcept
		{
			dbExpects( index < 32 );
			dbExpects( m_newLoadDelay.index == 0 );
			if ( index != 0 )
			{
				m_newLoadDelay.index = index;
				m_newLoadDelay.value = value;

				// loading into the same register twice in a row drops the first one
				if ( m_loadDelay.index == index )
					m_loadDelay.index = 0;
			}
		}

		void Reset() noexcept
		{
			m_registers.fill( 0 );
			m_loadDelay = { 0, 0 };
			m_newLoadDelay = { 0, 0 };
		}

		inline void Update() noexcept
		{
			if ( m_loadDelay.index != 0 )
				m_registers[ m_loadDelay.index ] = m_loadDelay.value;

			m_loadDelay = m_newLoadDelay;
			m_newLoadDelay.index = 0;
		}

		inline void Flush() noexcept
		{
			if ( m_loadDelay.index != 0 )
				m_registers[ m_loadDelay.index ] = m_loadDelay.value;

			m_newLoadDelay.index = 0;
		}

		// used by LWL and LWR to emulate special hardware that allows for both instructions to be used without a NOP inbetween
		uint32_t GetLoadDelayIndex() const noexcept { return m_loadDelay.index; }
		uint32_t GetLoadDelayValue() const noexcept { return m_loadDelay.value; }

		void Serialize( SaveStateSerializer& serializer );

	private:
		struct LoadDelay
		{
			uint32_t index = 0;
			uint32_t value = 0;
		};

	private:
		std::array<uint32_t, 32>	m_registers{};
		//								^
		LoadDelay					m_loadDelay;
		//								^
		LoadDelay					m_newLoadDelay;
	};

private:
	// skip instruction in branch delay slot and flush pipeline
	void SetProgramCounter( uint32_t address )
	{
		dbExpects( address % 4 == 0 );
		m_pc = address;
		m_nextPC = address + 4;

		m_inBranch = false;
		m_inDelaySlot = false;
		m_registers.Flush();
	}

	void InterceptBios( uint32_t pc );

	void ExecuteInstruction( Instruction instr ) noexcept;

	void AddTrap( uint32_t x, uint32_t y, uint32_t destRegister ) noexcept;

	void SubtractTrap( uint32_t x, uint32_t y, uint32_t destRegister ) noexcept;

	void BranchImp( bool condition, uint32_t signedOffset ) noexcept;

	void JumpImp( uint32_t target ) noexcept;

	void CheckProgramCounterAlignment() noexcept;

	void RaiseException( Cop0::ExceptionCode code, uint32_t coprocessor = 0 ) noexcept;

	uint32_t GetVAddr( Instruction instr ) const noexcept;

	template <typename T>
	T LoadImp( uint32_t address ) const noexcept;

	template <typename T>
	void LoadImp( Instruction instr ) noexcept;

	template <typename T>
	void StoreImp( uint32_t address, T value ) noexcept;

	template <typename T>
	void StoreImp( Instruction instr ) noexcept;

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

	// ANDI
	void BitwiseAndImmediate( Instruction ) noexcept;

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

	void IllegalInstruction( Instruction ) noexcept;

private:

	using InstructionFunction = void( MipsR3000Cpu::* )( Instruction ) noexcept;

private:
	MemoryMap& m_memoryMap;
	InterruptControl& m_interruptControl;
	EventManager& m_eventManager;

	Cop0 m_cop0;
	GTE m_gte;

	Registers m_registers;

	uint32_t m_currentPC = 0; // pc of instruction being executed
	uint32_t m_pc = 0; // pc of instruction being fetched
	uint32_t m_nextPC = 0;

	bool m_inBranch = false;
	bool m_inDelaySlot = false;

	uint32_t m_hi = 0;
	uint32_t m_lo = 0;

	std::string m_consoleOutput; // flushes on newline character

	// not serialized
	fs::path m_exeFilename;
};

inline void MipsR3000Cpu::CheckProgramCounterAlignment() noexcept
{
	if ( m_nextPC % 4 != 0 )
		RaiseException( Cop0::ExceptionCode::AddressErrorLoad );
}

inline uint32_t MipsR3000Cpu::GetVAddr( Instruction instr ) const noexcept
{
	return m_registers[ instr.base() ] + instr.immediateSigned();
}

template <typename T>
T MipsR3000Cpu::LoadImp( uint32_t address ) const noexcept
{
	dbExpects( address % sizeof( T ) == 0 );
	if ( !m_cop0.GetIsolateCache() )
	{
		return m_memoryMap.Read<T>( address );
	}
	else
	{
		dbBreakMessage( "read cache [%X]", address );
		return 0;
	}
}

template <typename T>
void MipsR3000Cpu::LoadImp( Instruction instr ) noexcept
{
	const uint32_t addr = GetVAddr( instr );
	if ( addr % sizeof( T ) == 0 )
	{
		using ExtendedType = std::conditional_t<std::is_signed_v<T>, int32_t, uint32_t>;
		const auto value = static_cast<uint32_t>( static_cast<ExtendedType>( LoadImp<T>( addr ) ) );
		m_registers.Load( instr.rt(), value );
	}
	else
	{
		RaiseException( Cop0::ExceptionCode::AddressErrorLoad );
	}
}

template <typename T>
void MipsR3000Cpu::StoreImp( uint32_t address, T value ) noexcept
{
	if ( address % sizeof( T ) == 0 )
	{
		if ( !m_cop0.GetIsolateCache() || address & 0x80000000 )
		{
			m_memoryMap.Write<T>( address, value );
		}
		else
		{
			// dbLog( "write cache [%X <= %X]", address, value );
			m_memoryMap.WriteICache( address, value );
		}
	}
	else
	{
		RaiseException( Cop0::ExceptionCode::AddressErrorStore );
	}
}

template <typename T>
void MipsR3000Cpu::StoreImp( Instruction instr ) noexcept
{
	StoreImp<T>( GetVAddr( instr ), static_cast<T>( m_registers[ instr.rt() ] ) );
}

}