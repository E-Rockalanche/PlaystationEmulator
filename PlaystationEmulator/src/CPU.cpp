#include "CPU.h"

#include "MemoryMap.h"

#include "assert.h"

#include <fstream>
#include <type_traits>

namespace PSX
{

inline void MipsR3000Cpu::MultiplyUnit::MultiplySigned( uint32_t x, uint32_t y )
{
	const uint64_t result = static_cast<uint64_t>(
		static_cast<int64_t>( static_cast<int32_t>( x ) ) *
		static_cast<int64_t>( static_cast<int32_t>( y ) ) );

	lo = static_cast<uint32_t>( result );
	hi = static_cast<uint32_t>( result >> 32 );
}

inline void MipsR3000Cpu::MultiplyUnit::MultiplyUnsigned( uint32_t x, uint32_t y )
{
	const uint64_t result = static_cast<uint64_t>( static_cast<uint64_t>( x ) * static_cast<uint64_t>( y ) );

	lo = static_cast<uint32_t>( result );
	hi = static_cast<uint32_t>( result >> 32 );
}

inline void MipsR3000Cpu::MultiplyUnit::DivideSigned( uint32_t x, uint32_t y )
{
	if ( y != 0 )
	{
		lo = x / y;
		hi = x % y;
	}
	else
	{
		lo = ( x >= 0 ) ? 0xffffffffu : 1u;
		hi = x;
	}
}

inline void MipsR3000Cpu::MultiplyUnit::DivideUnsigned( uint32_t x, uint32_t y )
{
	if ( y != 0 )
	{
		lo = x / y;
		hi = x % y;
	}
	else
	{
		lo = 0xffffffffu;
		hi = x;
	}
}

#define SET_INSTR_ENTRY( instructionArray, opcode, function, nameArray, name )						\
[this]{																								\
	auto& p = instructionArray[ static_cast<std::size_t>( opcode ) ];								\
	dbAssertMessage( p == nullptr, #opcode " [%X] already set", static_cast<uint32_t>( opcode ) );	\
	p = &MipsR3000Cpu::function;																	\
	nameArray[ static_cast<std::size_t>( opcode ) ] = name;											\
}()

#define SET_INSTR_EXT( name, opcode, function ) SET_INSTR_ENTRY( m_instructions, Opcode::opcode, function, m_instructionNames, name )
#define SET_SP_INSTR_EXT( name, opcode, function ) SET_INSTR_ENTRY( m_specialInstructions, SpecialOpcode::opcode, function, m_specialInstructionNames, name )

#define SET_INSTR( name, func ) SET_INSTR_EXT( name, func, func )
#define SET_SP_INSTR( name, func ) SET_SP_INSTR_EXT( name, func, func )

MipsR3000Cpu::MipsR3000Cpu( MemoryMap& memoryMap ) : m_memoryMap{ memoryMap }
{
	SET_INSTR( "SPECIAL", Special );
	SET_INSTR( "ADDI", AddImmediate );
	SET_INSTR( "ADDIU", AddImmediateUnsigned );
	SET_INSTR( "ANDI", AndImmediate );
	SET_INSTR( "BE", BranchEqual );
	SET_INSTR( "BGTZ", BranchGreaterThanZero );
	SET_INSTR( "BLEZ", BranchLessEqualZero );
	SET_INSTR( "BNE", BranchNotEqual );
	SET_INSTR( "J", Jump );
	SET_INSTR( "JAL", JumpAndLink );
	SET_INSTR( "LB", LoadByte );
	SET_INSTR( "LBU", LoadByteUnsigned );
	SET_INSTR( "LH", LoadHalfword );
	SET_INSTR( "LHU", LoadHalfwordUnsigned );
	SET_INSTR( "LUI", LoadUpperImmediate );
	SET_INSTR( "LW", LoadWord );
	SET_INSTR( "LWL", LoadWordLeft );
	SET_INSTR( "LWR", LoadWordRight );
	SET_INSTR( "ORI", BitwiseOrImmediate );
	SET_INSTR( "SB", StoreByte );
	SET_INSTR( "SH", StoreHalfword );
	SET_INSTR( "SLTI", SetLessThanImmediate );
	SET_INSTR( "SLTIU", SetLessThanImmediateUnsigned );
	SET_INSTR( "SW", StoreWord );
	SET_INSTR( "SWL", StoreWordLeft );
	SET_INSTR( "SWR", StoreWordRight );
	SET_INSTR( "XORI", BitwiseXorImmediate );

	SET_INSTR_EXT( "COP0", CoprocessorUnit0, CoprocessorUnit );
	SET_INSTR_EXT( "COP1", CoprocessorUnit1, CoprocessorUnit );
	SET_INSTR_EXT( "COP2", CoprocessorUnit2, CoprocessorUnit );
	SET_INSTR_EXT( "COP3", CoprocessorUnit3, CoprocessorUnit );

	SET_INSTR_EXT( "LWC0", LoadWordToCoprocessor0, LoadWordToCoprocessor );
	SET_INSTR_EXT( "LWC1", LoadWordToCoprocessor1, LoadWordToCoprocessor );
	SET_INSTR_EXT( "LWC2", LoadWordToCoprocessor2, LoadWordToCoprocessor );
	SET_INSTR_EXT( "LWC3", LoadWordToCoprocessor3, LoadWordToCoprocessor );

	SET_INSTR_EXT( "SWC0", StoreWordFromCoprocessor0, StoreWordFromCoprocessor );
	SET_INSTR_EXT( "SWC1", StoreWordFromCoprocessor1, StoreWordFromCoprocessor );
	SET_INSTR_EXT( "SWC2", StoreWordFromCoprocessor2, StoreWordFromCoprocessor );
	SET_INSTR_EXT( "SWC3", StoreWordFromCoprocessor3, StoreWordFromCoprocessor );

	for ( auto& p : m_instructions )
		if ( p == nullptr )
			p = &MipsR3000Cpu::UnhandledInstruction;

	SET_SP_INSTR( "ADD", Add );
	SET_SP_INSTR( "ADDU", AddUnsigned );
	SET_SP_INSTR( "AND", BitwiseAnd );
	SET_SP_INSTR( "BREAK", Break );
	SET_SP_INSTR( "DIV", Divide );
	SET_SP_INSTR( "DIVU", DivideUnsigned );
	SET_SP_INSTR( "JALR", JumpAndLinkRegister );
	SET_SP_INSTR( "JR", JumpRegister );
	SET_SP_INSTR( "MFHI", MoveFromHi );
	SET_SP_INSTR( "MFLO", MoveFromLo );
	SET_SP_INSTR( "MTHI", MoveToHi );
	SET_SP_INSTR( "MTLO", MoveToLo );
	SET_SP_INSTR( "MULT", Multiply );
	SET_SP_INSTR( "MULTU", MultiplyUnsigned );
	SET_SP_INSTR( "NOR", BitwiseNor );
	SET_SP_INSTR( "OR", BitwiseOr );
	SET_SP_INSTR( "SLL", ShiftLeftLogical );
	SET_SP_INSTR( "SLLV", ShiftLeftLogicalVariable );
	SET_SP_INSTR( "SLT", SetLessThan );
	SET_SP_INSTR( "SLTU", SetLessThanUnsigned );
	SET_SP_INSTR( "SRA", ShiftRightArithmetic );
	SET_SP_INSTR( "SRAV", ShiftRightArithmeticVariable );
	SET_SP_INSTR( "SRL", ShiftRightLogical );
	SET_SP_INSTR( "SRLV", ShiftRightLogicalVariable );
	SET_SP_INSTR( "SUB", Subtract );
	SET_SP_INSTR( "SUBU", SubtractUnsigned );
	SET_SP_INSTR( "SYSTEM", SystemCall );
	SET_SP_INSTR( "XOR", BitwiseXor );

	for ( auto& p : m_specialInstructions )
		if ( p == nullptr )
			p = &MipsR3000Cpu::UnhandledInstruction;

	Reset();
}

#undef SET_INSTR
#undef SET_SP_INSTR
#undef SET_INSTR_EXT
#undef SET_SP_INSTR_EXT
#undef SET_INSTR_ENTRY

void MipsR3000Cpu::Reset()
{
	m_pc = 0xbfc00000;

	m_registers.Reset();
}

void MipsR3000Cpu::Tick() noexcept
{
	// the MIPS cpu is pipelined. The next instruction is fetched while the current one executes
	const Instruction curInstr = m_nextInstruction;
	m_nextInstruction = m_memoryMap.Read<uint32_t>( m_pc );
	m_pc += 4;

	const uint32_t opcode = curInstr.op();

	if ( curInstr.value == 0 )
		dbLog( "NOP" );
	else if ( opcode == 0 )
		dbLog( "%s\trs:%u\trt:%u\trd:%u\tshamt:%u", m_specialInstructionNames[ curInstr.funct() ], curInstr.rs(), curInstr.rt(), curInstr.rd(), curInstr.shamt() );
	else
		dbLog( "%s\trs:%u\trt:%u\timmediate:%i", m_instructionNames[ opcode ], curInstr.rs(), curInstr.rt(), static_cast<int32_t>( curInstr.immediate() ) );

	std::invoke( m_instructions[ opcode ], this, curInstr );

	m_registers.Update();
}

inline void MipsR3000Cpu::AddTrap( uint32_t x, uint32_t y, uint32_t destRegister ) noexcept
{
	const int32_t sx = static_cast<int32_t>( x );
	const int32_t sy = static_cast<int32_t>( y );
	const int32_t sum = sx + sy;

	if ( sx < 0 != sy < 0 || ( sx < 0 == sum < 0 ) )
	{
		m_registers.Set( destRegister, static_cast<uint32_t>( sum ) );
	}
	else
	{
		// TODO: trap
	}
}

inline void MipsR3000Cpu::SubtractTrap( uint32_t x, uint32_t y, uint32_t destRegister ) noexcept
{
	const int32_t sx = static_cast<int32_t>( x );
	const int32_t sy = static_cast<int32_t>( y );
	const int32_t diff = sx - sy;

	if ( sx < 0 == sy < 0 || ( sx < 0 == diff < 0 ) )
	{
		m_registers.Set( destRegister, static_cast<uint32_t>( diff ) );
	}
	else
	{
		// TODO: trap
	}
}

inline void MipsR3000Cpu::BranchImp( bool condition, int16_t offset ) noexcept
{
	if ( condition )
	{
		// offset is added to address of delay slot
		// PC is at instruction after delay slot
		m_pc = ( m_pc - 4 ) + ( Extend( offset ) << 2 );
	}
}

inline void MipsR3000Cpu::JumpImp( uint32_t target ) noexcept
{
	// PC is at instruction after delay slot
	// TODO: check if 4 should be subtracted from PC
	m_pc = ( m_pc & 0xf0000000 ) | target;
}

inline void MipsR3000Cpu::CheckProgramCounterAlignment() noexcept
{
	// TODO: check if PC is in bounds
	if ( ( ( m_pc & 0x3 ) != 0 ) )
	{
		// TODO: trap
	}
}

void MipsR3000Cpu::Special( Instruction instr ) noexcept
{
	std::invoke( m_specialInstructions[ instr.funct() ], this, instr );
}

void MipsR3000Cpu::RegisterImmediate( Instruction instr ) noexcept
{
	switch ( static_cast<RegImmOpcode>( instr.rt() ) )
	{
		case RegImmOpcode::BranchGreaterEqualZero:
			BranchGreaterEqualZero( instr );
			break;

		case RegImmOpcode::BranchGreaterEqualZeroAndLink:
			BranchGreaterEqualZeroAndLink( instr );
			break;

		case RegImmOpcode::BranchLessThanZero:
			BranchLessThanZero( instr );
			break;

		case RegImmOpcode::BranchLessThanZeroAndLink:
			BranchLessThanZeroAndLink( instr );
			break;

		default:
			dbBreakMessage( "Invalid RegImm opcode: %X", static_cast<uint32_t>( instr.rt() ) );
			break;
	}
}

void MipsR3000Cpu::CoprocessorUnit( Instruction instr ) noexcept
{
	switch ( static_cast<CoprocessorOpcode>( instr.subop() ) )
	{
		case CoprocessorOpcode::MoveControlFromCoprocessor:
			MoveControlFromCoprocessor( instr );
			break;

		case CoprocessorOpcode::MoveControlToProcessor:
			MoveControlToCoprocessor( instr );
			break;

		case CoprocessorOpcode::MoveFromCoprocessor:
			MoveFromCoprocessor( instr );
			break;

		case CoprocessorOpcode::MoveToCoprocessor:
			MoveToCoprocessor( instr );
			break;

		default:
		{
			if ( instr.subop() & 0b100000 )
				CoprocessorOperation( instr );
			else
				dbBreakMessage( "Invalid coprocessor subop: %X", static_cast<uint32_t>( instr.rs() ) );
		}
	}
}

void MipsR3000Cpu::Add( Instruction instr ) noexcept
{
	AddTrap( m_registers[ instr.rs() ], m_registers[ instr.rt() ], instr.rd() );
}

void MipsR3000Cpu::AddImmediate( Instruction instr ) noexcept
{
	AddTrap( m_registers[ instr.rs() ], Extend( instr.immediate() ), instr.rt() );
}

void MipsR3000Cpu::AddImmediateUnsigned( Instruction instr ) noexcept
{
	m_registers.Set( instr.rt(), m_registers[ instr.rs() ] + Extend( instr.immediate() ) );
}

void MipsR3000Cpu::AddUnsigned( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] + m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::BitwiseAnd( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] & m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::AndImmediate( Instruction instr ) noexcept
{
	m_registers.Set( instr.rt(), m_registers[ instr.rs() ] & static_cast<uint32_t>( instr.immediate() ) );
}

void MipsR3000Cpu::BranchEqual( Instruction instr ) noexcept
{
	BranchImp( m_registers[ instr.rs() ] == m_registers[ instr.rt() ], instr.offset() );
}

void MipsR3000Cpu::BranchGreaterEqualZero( Instruction instr ) noexcept
{
	BranchImp( m_registers[ instr.rs() ] >= 0, instr.offset() );
}

void MipsR3000Cpu::BranchGreaterEqualZeroAndLink( Instruction instr ) noexcept
{
	// store return address after delay slot
	// PC is already after delay slot
	m_registers.Set( Registers::ReturnAddress, m_pc );
	BranchImp( m_registers[ instr.rs() ] >= 0, instr.offset() );
}

void MipsR3000Cpu::BranchGreaterThanZero( Instruction instr ) noexcept
{
	BranchImp( m_registers[ instr.rs() ] > 0, instr.offset() );
}

void MipsR3000Cpu::BranchLessEqualZero( Instruction instr ) noexcept
{
	BranchImp( m_registers[ instr.rs() ] <= 0, instr.offset() );
}

void MipsR3000Cpu::BranchLessThanZero( Instruction instr ) noexcept
{
	BranchImp( m_registers[ instr.rs() ] < 0, instr.offset() );
}

void MipsR3000Cpu::BranchLessThanZeroAndLink( Instruction instr ) noexcept
{
	// store return address after delay slot
	// PC is already after delay slot
	m_registers.Set( Registers::ReturnAddress, m_pc );
	BranchImp( m_registers[ instr.rs() ] < 0, instr.offset() );
}

void MipsR3000Cpu::BranchNotEqual( Instruction instr ) noexcept
{
	BranchImp( m_registers[ instr.rs() ] != m_registers[ instr.rt() ], instr.offset() );
}

void MipsR3000Cpu::Break( Instruction ) noexcept
{
	// TODO: breakpoint trap occurs, immediately and unconditionally transferring control to exception handler
	// code field is available for use as software parameters, but is retrieved by the exception handler only by loading the contents of the memory word containging the instruction
}

void MipsR3000Cpu::MoveControlFromCoprocessor( Instruction ) noexcept
{
	// TODO: the contents of coprocessor control register rd of coprocessor unit are loaded into general register rt
	// instruction is invalid for CP0
}

void MipsR3000Cpu::CoprocessorOperation( Instruction ) noexcept
{
	// TODO: a coprocessor operation is performed. The operation may specify and reference internal coprocessor registers, and may change the state of the coprocessor condition line,
	// but does not modify state within the processor or the cache/memory system
}

void MipsR3000Cpu::MoveControlToCoprocessor( Instruction  ) noexcept
{
	// TODO: the contents of general register rt are loaded into control register rd of coprocessor unit
	// instruction is invalid for CP0
}

void MipsR3000Cpu::Divide( Instruction instr ) noexcept
{
	m_multiplyUnit.DivideSigned( m_registers[ instr.rs() ], m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::DivideUnsigned( Instruction instr ) noexcept
{
	m_multiplyUnit.DivideUnsigned( m_registers[ instr.rs() ], m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::Jump( Instruction instr ) noexcept
{
	JumpImp( instr.target() );
}

void MipsR3000Cpu::JumpAndLink( Instruction instr ) noexcept
{
	// store return address after delay slot
	// PC is already after delay slot
	m_registers.Set( Registers::ReturnAddress, m_pc );
	JumpImp( instr.target() );
}

void MipsR3000Cpu::JumpAndLinkRegister( Instruction instr ) noexcept
{
	// store return address after delay slot
	// PC is already after delay slot
	m_registers.Set( instr.rd(), m_pc );
	m_pc = m_registers[ instr.rs() ];
	CheckProgramCounterAlignment();
}

void MipsR3000Cpu::JumpRegister( Instruction instr ) noexcept
{
	m_pc = m_registers[ instr.rs() ];
	CheckProgramCounterAlignment();
}

void MipsR3000Cpu::LoadByte( Instruction instr ) noexcept
{
	m_registers.Load( instr.rt(), LoadImp<int8_t>( instr ) );
}

void MipsR3000Cpu::LoadByteUnsigned( Instruction instr ) noexcept
{
	m_registers.Load( instr.rt(), LoadImp<uint8_t>( instr ) );
}

void MipsR3000Cpu::LoadHalfword( Instruction instr ) noexcept
{
	m_registers.Load( instr.rt(), LoadImp<int16_t>( instr ) );
}

void MipsR3000Cpu::LoadHalfwordUnsigned( Instruction instr ) noexcept
{
	m_registers.Load( instr.rt(), LoadImp<uint16_t>( instr ) );
}

void MipsR3000Cpu::LoadUpperImmediate( Instruction instr ) noexcept
{
	m_registers.Load( instr.rt(), static_cast<uint32_t>( instr.immediate() ) << 16 );
}

void MipsR3000Cpu::LoadWord( Instruction instr ) noexcept
{
	m_registers.Load( instr.rt(), LoadImp<int32_t>( instr ) );
}

void MipsR3000Cpu::LoadWordToCoprocessor( Instruction instr ) noexcept
{
	const auto value = LoadImp<int32_t>( instr );
	// TODO: make the value available to coprocessor unit
}

void MipsR3000Cpu::LoadWordLeft( Instruction instr ) noexcept
{
	uint32_t addr = GetVAddr( instr );

	union
	{
		uint32_t reg;
		uint8_t regBytes[ 4 ];
	};

	if ( m_registers.GetOutputIndex() == instr.rt() )
		reg = m_registers.GetOutputValue();
	else
		reg = m_registers[ instr.rt() ];

	uint8_t* regByte = regBytes;
	do
	{
		*regByte++ = m_memoryMap.Read<uint8_t>( addr++ );
	}
	while ( addr % 4 != 0 );

	m_registers.Load( instr.rt(), reg );
}

void MipsR3000Cpu::LoadWordRight( Instruction instr ) noexcept
{
	uint32_t addr = GetVAddr( instr );

	union
	{
		uint32_t reg;
		uint8_t regBytes[ 4 ];
	};

	if ( m_registers.GetOutputIndex() == instr.rt() )
		reg = m_registers.GetOutputValue();
	else
		reg = m_registers[ instr.rt() ];

	uint8_t* regByte = regBytes + 3;
	do
	{
		*regByte-- = m_memoryMap.Read<uint8_t>( addr-- );
	}
	while ( addr % 4 != 3 );

	m_registers.Load( instr.rt(), reg );
}

void MipsR3000Cpu::MoveFromCoprocessor( Instruction  ) noexcept
{
	// TODO: the contents of coprocessor register rd of coprocessor unit are loaded into general register rt
}

void MipsR3000Cpu::MoveFromHi( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_multiplyUnit.hi );
}

void MipsR3000Cpu::MoveFromLo( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_multiplyUnit.lo );
}

void MipsR3000Cpu::MoveToCoprocessor( Instruction  ) noexcept
{
	// TODO: the contents of general register rt are loaded into coprocessor register rd of coprocessor unit
}

void MipsR3000Cpu::MoveToHi( Instruction instr ) noexcept
{
	m_multiplyUnit.hi = m_registers[ instr.rs() ];
}

void MipsR3000Cpu::MoveToLo( Instruction instr ) noexcept
{
	m_multiplyUnit.lo = m_registers[ instr.rs() ];
}

void MipsR3000Cpu::Multiply( Instruction instr ) noexcept
{
	m_multiplyUnit.MultiplySigned( m_registers[ instr.rs() ], m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::MultiplyUnsigned( Instruction instr ) noexcept
{
	m_multiplyUnit.MultiplyUnsigned( m_registers[ instr.rs() ], m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::BitwiseNor( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), ~( m_registers[ instr.rs() ] | m_registers[ instr.rt() ] ) );
}

void MipsR3000Cpu::BitwiseOr( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] | m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::BitwiseOrImmediate( Instruction instr ) noexcept
{
	m_registers.Set( instr.rt(), m_registers[ instr.rs() ] | static_cast<uint32_t>( instr.immediate() ) );
}

void MipsR3000Cpu::StoreByte( Instruction instr ) noexcept
{
	StoreImp( instr.rs(), instr.immediate(), static_cast<uint8_t>( m_registers[ instr.rt() ] ) );
}

void MipsR3000Cpu::StoreHalfword( Instruction instr ) noexcept
{
	StoreImp( instr.rs(), instr.immediate(), static_cast<uint16_t>( m_registers[ instr.rt() ] ) );
}

void MipsR3000Cpu::ShiftLeftLogical( Instruction instr ) noexcept
{
	// SSL is commonly used as NOP
	if ( instr.value != 0 )
		m_registers.Set( instr.rd(), m_registers[ instr.rt() ] << instr.shamt() );
}

void MipsR3000Cpu::ShiftLeftLogicalVariable( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rt() ] << m_registers[ instr.rs() ] );
}

void MipsR3000Cpu::SetLessThan( Instruction instr ) noexcept
{
	const bool set = static_cast<int32_t>( m_registers[ instr.rs() ] ) < static_cast<int32_t>( m_registers[ instr.rt() ] );
	m_registers.Set( instr.rd(), set ? 1 : 0 );
}

void MipsR3000Cpu::SetLessThanImmediate( Instruction instr ) noexcept
{
	const bool set = static_cast<int32_t>( m_registers[ instr.rs() ] ) < Extend( instr.immediate() );
	m_registers.Set( instr.rt(), set ? 1 : 0 );
}

void MipsR3000Cpu::SetLessThanImmediateUnsigned( Instruction instr ) noexcept
{
	const bool set = m_registers[ instr.rs() ] < static_cast<uint32_t>( Extend( instr.immediate() ) );
	m_registers.Set( instr.rt(), set ? 1 : 0 );
}

void MipsR3000Cpu::SetLessThanUnsigned( Instruction instr ) noexcept
{
	const bool set = m_registers[ instr.rs() ] < m_registers[ instr.rt() ];
	m_registers.Set( instr.rd(), set ? 1 : 0 );
}

void MipsR3000Cpu::ShiftRightArithmetic( Instruction instr ) noexcept
{
	// TODO: check compiler for arithemtic shift right
	m_registers.Set( instr.rd(), static_cast<int32_t>( m_registers[ instr.rt() ] ) >> instr.shamt() );
}

void MipsR3000Cpu::ShiftRightArithmeticVariable( Instruction instr ) noexcept
{
	// TODO: check compiler for arithemtic shift right
	m_registers.Set( instr.rd(), static_cast<int32_t>( m_registers[ instr.rt() ] ) >> m_registers[ instr.rs() ] );
}

void MipsR3000Cpu::ShiftRightLogical( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rt() ] >> instr.shamt() );
}

void MipsR3000Cpu::ShiftRightLogicalVariable( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rt() ] >> m_registers[ instr.rs() ] );
}

void MipsR3000Cpu::Subtract( Instruction instr ) noexcept
{
	if ( instr.rd() != 0 )
		SubtractTrap( m_registers[ instr.rs() ], m_registers[ instr.rt() ], m_registers[ instr.rd() ] );
}

void MipsR3000Cpu::SubtractUnsigned( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] - m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::StoreWord( Instruction instr ) noexcept
{
	m_memoryMap.Write<uint32_t>( GetVAddr( instr ), m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::StoreWordFromCoprocessor( Instruction ) noexcept
{
	// TODO: coprocessor unit sources a word, which the processor writes tothe addressed memory
}

void MipsR3000Cpu::StoreWordLeft( Instruction instr ) noexcept
{
	uint32_t addr = GetVAddr( instr );

	union
	{
		uint32_t reg;
		uint8_t regBytes[ 4 ];
	};
	reg = m_registers[ instr.rt() ];

	const uint8_t* regByte = regBytes;
	do
	{
		m_memoryMap.Write( addr++, *( regByte++ ) );
	}
	while ( addr % 4 != 0 );
}

void MipsR3000Cpu::StoreWordRight( Instruction instr ) noexcept
{
	uint32_t addr = GetVAddr( instr );

	union
	{
		uint32_t reg;
		uint8_t regBytes[ 4 ];
	};
	reg = m_registers[ instr.rt() ];

	const uint8_t* regByte = regBytes + 3;
	do
	{
		m_memoryMap.Write( addr--, *( regByte-- ) );
	}
	while ( addr % 4 != 3 );
}

void MipsR3000Cpu::SystemCall( Instruction ) noexcept
{
	// a system call exception occurs, immediately and unconditionally transferring control to the exception handler
}

void MipsR3000Cpu::BitwiseXor( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] ^ m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::BitwiseXorImmediate( Instruction instr ) noexcept
{
	m_registers.Set( instr.rt(), m_registers[ instr.rs() ] ^ static_cast<uint32_t>( instr.immediate() ) );
}

void MipsR3000Cpu::UnhandledInstruction( Instruction instr ) noexcept
{
	dbBreakMessage( "Unhandled instruction: %X", instr.value );
}

}