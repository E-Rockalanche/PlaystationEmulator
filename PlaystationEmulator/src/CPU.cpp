#include "CPU.h"

#include "MemoryMap.h"

#include "assert.h"

#include <fstream>
#include <type_traits>

namespace PSX
{



uint32_t MipsR3000Cpu::Registers::operator[]( uint32_t index ) const noexcept
{
	return m_input[ index ];
}

void MipsR3000Cpu::Reset()
{
	m_currentPC = 0;
	m_pc = 0xbfc00000;
	m_nextPC = m_pc + 4;

	m_inBranch = false;
	m_inDelaySlot = false;

	m_registers.Reset();

	m_lo = 0;
	m_hi = 0;

	m_cop0.Reset();

	m_instructionCache.Reset();
}

void MipsR3000Cpu::Tick() noexcept
{
	// the MIPS cpu is pipelined. The next instruction is fetched while the current one executes
	// this causes instructions after branches and jumps to always be executed

	m_currentPC = m_pc;
	m_pc = m_nextPC;
	m_nextPC += 4;

	m_inDelaySlot = m_inBranch;
	m_inBranch = false;

	ExecuteInstruction( Instruction{ m_memoryMap.Read<uint32_t>( m_currentPC ) } );

	m_registers.Update();

	m_timers.AddCycles( 1 ); // TODO: if we did an UpdateTimersNow() then the target cycles will be 0 here. We could miss a low target

	if ( m_cop0.CheckException() )
	{
		dbBreak(); // TODO: interrupt
	}
}

void MipsR3000Cpu::ExecuteInstruction( Instruction instr ) noexcept
{
#define OP_CASE( opcode ) case Opcode::opcode:	opcode( instr );	break;

	switch ( static_cast<Opcode>( instr.op() ) )
	{
		OP_CASE( Special )

		OP_CASE( RegisterImmediate )

		OP_CASE( AddImmediate )
		OP_CASE( AddImmediateUnsigned )
		OP_CASE( BitwiseAndImmediate )
		OP_CASE( BranchEqual )
		OP_CASE( BranchGreaterThanZero )
		OP_CASE( BranchLessEqualZero )
		OP_CASE( BranchNotEqual )
		OP_CASE( Jump )
		OP_CASE( JumpAndLink )
		OP_CASE( LoadByte )
		OP_CASE( LoadByteUnsigned )
		OP_CASE( LoadHalfword )
		OP_CASE( LoadHalfwordUnsigned )
		OP_CASE( LoadUpperImmediate )
		OP_CASE( LoadWord )
		OP_CASE( LoadWordLeft )
		OP_CASE( LoadWordRight )
		OP_CASE( BitwiseOrImmediate )
		OP_CASE( StoreByte )
		OP_CASE( StoreHalfword )
		OP_CASE( SetLessThanImmediate )
		OP_CASE( SetLessThanImmediateUnsigned )
		OP_CASE( StoreWord )
		OP_CASE( StoreWordLeft )
		OP_CASE( StoreWordRight )
		OP_CASE( BitwiseXorImmediate )

		case Opcode::CoprocessorUnit0:
		case Opcode::CoprocessorUnit1:
		case Opcode::CoprocessorUnit2:
		case Opcode::CoprocessorUnit3:
			CoprocessorUnit( instr );
			break;

		case Opcode::LoadWordToCoprocessor0:
		case Opcode::LoadWordToCoprocessor1:
		case Opcode::LoadWordToCoprocessor2:
		case Opcode::LoadWordToCoprocessor3:
			LoadWordToCoprocessor( instr );
			break;

		case Opcode::StoreWordFromCoprocessor0:
		case Opcode::StoreWordFromCoprocessor1:
		case Opcode::StoreWordFromCoprocessor2:
		case Opcode::StoreWordFromCoprocessor3:
			StoreWordFromCoprocessor( instr );
			break;

		default:
			IllegalInstruction( instr );
			break;
	}

#undef OP_CASE
}

void MipsR3000Cpu::RaiseException( Cop0::ExceptionCode code, uint32_t coprocessor, bool branch ) noexcept
{
	// exceptions in delay slot return to branch instruction
	const uint32_t returnAddress = m_currentPC - ( m_inDelaySlot ? 4 : 0 );

	m_cop0.SetException( returnAddress, code, coprocessor, branch );

	// exception jumps don't have a delay slot
	m_pc = m_cop0.GetExceptionVector();
	dbAssert( m_pc % 4 == 0 );
	m_nextPC = m_pc + 4;
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
		RaiseException( Cop0::ExceptionCode::ArithmeticOverflow );
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
		RaiseException( Cop0::ExceptionCode::ArithmeticOverflow );
	}
}

inline void MipsR3000Cpu::BranchImp( bool condition, uint32_t signedOffset ) noexcept
{
	m_inBranch = true;
	if ( condition )
	{
		// offset is added to address of delay slot
		m_nextPC = ( m_currentPC + 4 ) + ( signedOffset << 2 );
		dbAssert( m_nextPC % 4 == 0 );
	}
}

inline void MipsR3000Cpu::JumpImp( uint32_t target ) noexcept
{
	dbExpects( target % 4 == 0 ); // target must be word aligned

	m_inBranch = true;

	// 26 bit target is left shifted 2 bits and combined with the high-order bits of the address of the delay slot

	m_nextPC = ( ( m_currentPC + 4 ) & 0xf0000000 ) | target;

	CheckProgramCounterAlignment();
}

void MipsR3000Cpu::Special( Instruction instr ) noexcept
{
#define OP_CASE( opcode ) case SpecialOpcode::opcode:	opcode( instr );	break;

	switch ( static_cast<SpecialOpcode>( instr.funct() ) )
	{
		OP_CASE( Add )
		OP_CASE( AddUnsigned )
		OP_CASE( BitwiseAnd )
		OP_CASE( Break )
		OP_CASE( Divide )
		OP_CASE( DivideUnsigned )
		OP_CASE( JumpAndLinkRegister )
		OP_CASE( JumpRegister )
		OP_CASE( MoveFromHi )
		OP_CASE( MoveFromLo )
		OP_CASE( MoveToHi )
		OP_CASE( MoveToLo )
		OP_CASE( Multiply )
		OP_CASE( MultiplyUnsigned )
		OP_CASE( BitwiseNor )
		OP_CASE( BitwiseOr )
		OP_CASE( ShiftLeftLogical )
		OP_CASE( ShiftLeftLogicalVariable )
		OP_CASE( SetLessThan )
		OP_CASE( SetLessThanUnsigned )
		OP_CASE( ShiftRightArithmetic )
		OP_CASE( ShiftRightArithmeticVariable )
		OP_CASE( ShiftRightLogical )
		OP_CASE( ShiftRightLogicalVariable )
		OP_CASE( Subtract )
		OP_CASE( SubtractUnsigned )
		OP_CASE( SystemCall )
		OP_CASE( BitwiseXor )

		default:
			IllegalInstruction( instr );
			break;
	}

#undef OP_CASE
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
			IllegalInstruction( instr );
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

		case CoprocessorOpcode::MoveControlToCoprocessor:
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
			if ( instr.subop() & 0b10000 )
				CoprocessorOperation( instr );
			else
				IllegalInstruction( instr );

			break;
		}
	}
}

void MipsR3000Cpu::Add( Instruction instr ) noexcept
{
	AddTrap( m_registers[ instr.rs() ], m_registers[ instr.rt() ], instr.rd() );
}

void MipsR3000Cpu::AddImmediate( Instruction instr ) noexcept
{
	AddTrap( m_registers[ instr.rs() ], instr.immediateSigned(), instr.rt() );
}

void MipsR3000Cpu::AddImmediateUnsigned( Instruction instr ) noexcept
{
	m_registers.Set( instr.rt(), m_registers[ instr.rs() ] + instr.immediateSigned() );
}

void MipsR3000Cpu::AddUnsigned( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] + m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::BitwiseAnd( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] & m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::BitwiseAndImmediate( Instruction instr ) noexcept
{
	m_registers.Set( instr.rt(), m_registers[ instr.rs() ] & instr.immediateUnsigned() );
}

void MipsR3000Cpu::BranchEqual( Instruction instr ) noexcept
{
	BranchImp( m_registers[ instr.rs() ] == m_registers[ instr.rt() ], instr.offset() );
}

inline void MipsR3000Cpu::BranchGreaterEqualZero( Instruction instr ) noexcept
{
	BranchImp( static_cast<int32_t>( m_registers[ instr.rs() ] ) >= 0, instr.offset() );
}

inline void MipsR3000Cpu::BranchGreaterEqualZeroAndLink( Instruction instr ) noexcept
{
	// store return address after delay slot
	m_registers.Set( Registers::ReturnAddress, m_currentPC + 8 );
	BranchImp( static_cast<int32_t>( m_registers[ instr.rs() ] ) >= 0, instr.offset() );
}

void MipsR3000Cpu::BranchGreaterThanZero( Instruction instr ) noexcept
{
	BranchImp( static_cast<int32_t>( m_registers[ instr.rs() ] ) > 0, instr.offset() );
}

void MipsR3000Cpu::BranchLessEqualZero( Instruction instr ) noexcept
{
	BranchImp( static_cast<int32_t>( m_registers[ instr.rs() ] ) <= 0, instr.offset() );
}

inline void MipsR3000Cpu::BranchLessThanZero( Instruction instr ) noexcept
{
	BranchImp( static_cast<int32_t>( m_registers[ instr.rs() ] ) < 0, instr.offset() );
}

inline void MipsR3000Cpu::BranchLessThanZeroAndLink( Instruction instr ) noexcept
{
	dbExpects( instr.rs() != Registers::ReturnAddress );

	// store return address after delay slot
	m_registers.Set( Registers::ReturnAddress, m_currentPC + 8 );
	BranchImp( static_cast<int32_t>( m_registers[ instr.rs() ] ) < 0, instr.offset() );
}

void MipsR3000Cpu::BranchNotEqual( Instruction instr ) noexcept
{
	BranchImp( m_registers[ instr.rs() ] != m_registers[ instr.rt() ], instr.offset() );
}

void MipsR3000Cpu::Break( Instruction ) noexcept
{
	RaiseException( Cop0::ExceptionCode::Breakpoint );
}

void MipsR3000Cpu::MoveControlFromCoprocessor( Instruction instr ) noexcept
{
	// TODO: the contents of coprocessor control register rd of coprocessor unit are loaded into general register rt

	dbExpects( instr.z() != 0 ); // instruction is invalid for coprocessor 0
}

void MipsR3000Cpu::CoprocessorOperation( Instruction instr ) noexcept
{
	// TODO: a coprocessor operation is performed. The operation may specify and reference internal coprocessor registers, and may change the state of the coprocessor condition line,
	// but does not modify state within the processor or the cache/memory system

	switch ( instr.z() )
	{
		case 0:
			m_cop0.PrepareReturnFromException();
			break;

		default:
			dbBreak(); // TODO
			break;
	}
}

void MipsR3000Cpu::MoveControlToCoprocessor( Instruction instr ) noexcept
{
	// TODO: the contents of general register rt are loaded into control register rd of coprocessor unit

	dbExpects( instr.z() != 0 ); // instruction is invalid for coprocessor 0
}

void MipsR3000Cpu::Divide( Instruction instr ) noexcept
{
	const int32_t x = static_cast<int32_t>( m_registers[ instr.rs() ] );
	const int32_t y = static_cast<int32_t>( m_registers[ instr.rt() ] );

	if ( y != 0 )
	{
		m_lo = static_cast<uint32_t>( x / y );
		m_hi = static_cast<uint32_t>( x % y );
	}
	else
	{
		m_lo = ( x >= 0 ) ? 0xffffffffu : 1u;
		m_hi = static_cast<uint32_t>( x );
	}
}

void MipsR3000Cpu::DivideUnsigned( Instruction instr ) noexcept
{
	const uint32_t x = m_registers[ instr.rs() ];
	const uint32_t y = m_registers[ instr.rt() ];

	if ( y != 0 )
	{
		m_lo = x / y;
		m_hi = x % y;
	}
	else
	{
		m_lo = 0xffffffffu;
		m_hi = x;
	}
}

void MipsR3000Cpu::Jump( Instruction instr ) noexcept
{
	JumpImp( instr.target() );
}

void MipsR3000Cpu::JumpAndLink( Instruction instr ) noexcept
{
	// store return address after delay slot
	// PC is already after delay slot
	m_registers.Set( Registers::ReturnAddress, m_currentPC + 8 );
	JumpImp( instr.target() );
}

void MipsR3000Cpu::JumpAndLinkRegister( Instruction instr ) noexcept
{
	// store return address after delay slot
	// PC is already after delay slot
	m_inBranch = true;
	m_registers.Set( instr.rd(), m_currentPC + 8 );
	m_nextPC = m_registers[ instr.rs() ];

	CheckProgramCounterAlignment();
}

void MipsR3000Cpu::JumpRegister( Instruction instr ) noexcept
{
	m_inBranch = true;
	m_nextPC = m_registers[ instr.rs() ];

	CheckProgramCounterAlignment();
}

void MipsR3000Cpu::LoadByte( Instruction instr ) noexcept
{
	LoadImp<int8_t>( instr );
}

void MipsR3000Cpu::LoadByteUnsigned( Instruction instr ) noexcept
{
	LoadImp<uint8_t>( instr );
}

void MipsR3000Cpu::LoadHalfword( Instruction instr ) noexcept
{
	LoadImp<int16_t>( instr );
}

void MipsR3000Cpu::LoadHalfwordUnsigned( Instruction instr ) noexcept
{
	LoadImp<uint16_t>( instr );
}

void MipsR3000Cpu::LoadUpperImmediate( Instruction instr ) noexcept
{
	m_registers.Set( instr.rt(), static_cast<uint32_t>( instr.immediateUnsigned() ) << 16 );
}

void MipsR3000Cpu::LoadWord( Instruction instr ) noexcept
{
	LoadImp<int32_t>( instr );
}

void MipsR3000Cpu::LoadWordToCoprocessor( Instruction instr ) noexcept
{
	dbExpects( instr.z() != 0 ); // instruction not valid for coprocessor 0

	const auto address = GetVAddr( instr );
	if ( address % 4 == 0 )
	{
		const auto value = LoadImp<int32_t>( address );

		// TODO: make the value available to coprocessor unit
		(void)value;
	}
	else
	{
		RaiseException( Cop0::ExceptionCode::AddressErrorLoad );
	}
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
		*regByte++ = LoadImp<uint8_t>( addr++ );
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
		*regByte-- = LoadImp<uint8_t>( addr-- );
	}
	while ( addr % 4 != 3 );

	m_registers.Load( instr.rt(), reg );
}

void MipsR3000Cpu::MoveFromCoprocessor( Instruction instr ) noexcept
{
	uint32_t value;
	switch ( instr.z() )
	{
		case 0:
			value = m_cop0.Read( instr.rd() );
			break;

		default:
			dbBreak();
			value = 0;
			break;
	}
	m_registers.Load( instr.rt(), value );
}

void MipsR3000Cpu::MoveFromHi( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_hi );
}

void MipsR3000Cpu::MoveFromLo( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_lo );
}

void MipsR3000Cpu::MoveToCoprocessor( Instruction instr ) noexcept
{
	const uint32_t value = m_registers[ instr.rt() ];
	switch ( instr.z() )
	{
		case 0:
			m_cop0.Write( instr.rd(), value );
			break;

		default:
			dbBreak();
			break;
	}
}

void MipsR3000Cpu::MoveToHi( Instruction instr ) noexcept
{
	m_hi = m_registers[ instr.rs() ];
}

void MipsR3000Cpu::MoveToLo( Instruction instr ) noexcept
{
	m_lo = m_registers[ instr.rs() ];
}

void MipsR3000Cpu::Multiply( Instruction instr ) noexcept
{
	const auto x = static_cast<int64_t>( static_cast<int32_t>( m_registers[ instr.rs() ] ) );
	const auto y = static_cast<int64_t>( static_cast<int32_t>( m_registers[ instr.rt() ] ) );

	const int64_t result = x * y;

	m_lo = static_cast<uint32_t>( result );
	m_hi = static_cast<uint32_t>( result >> 32 );
}

void MipsR3000Cpu::MultiplyUnsigned( Instruction instr ) noexcept
{
	const auto x = static_cast<uint64_t>( m_registers[ instr.rs() ] );
	const auto y = static_cast<uint64_t>( m_registers[ instr.rt() ] );

	const uint64_t result = x * y;

	m_lo = static_cast<uint32_t>( result );
	m_hi = static_cast<uint32_t>( result >> 32 );
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
	m_registers.Set( instr.rt(), m_registers[ instr.rs() ] | instr.immediateUnsigned() );
}

void MipsR3000Cpu::StoreByte( Instruction instr ) noexcept
{
	StoreImp<uint8_t>( instr );
}

void MipsR3000Cpu::StoreHalfword( Instruction instr ) noexcept
{
	StoreImp<uint16_t>( instr );
}

void MipsR3000Cpu::ShiftLeftLogical( Instruction instr ) noexcept
{
	// SSL is commonly used as NOP
	if ( instr.value != 0 )
		m_registers.Set( instr.rd(), m_registers[ instr.rt() ] << instr.shamt() );
}

void MipsR3000Cpu::ShiftLeftLogicalVariable( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rt() ] << ( m_registers[ instr.rs() ] & 0x1f ) );
}

void MipsR3000Cpu::SetLessThan( Instruction instr ) noexcept
{
	const bool set = static_cast<int32_t>( m_registers[ instr.rs() ] ) < static_cast<int32_t>( m_registers[ instr.rt() ] );
	m_registers.Set( instr.rd(), static_cast<uint32_t>( set ) );
}

void MipsR3000Cpu::SetLessThanImmediate( Instruction instr ) noexcept
{
	const bool set = static_cast<int32_t>( m_registers[ instr.rs() ] ) < instr.immediateSigned();
	m_registers.Set( instr.rt(), static_cast<uint32_t>( set ) );
}

void MipsR3000Cpu::SetLessThanImmediateUnsigned( Instruction instr ) noexcept
{
	const bool set = m_registers[ instr.rs() ] < instr.immediateSigned();
	m_registers.Set( instr.rt(), static_cast<uint32_t>( set ) );
}

void MipsR3000Cpu::SetLessThanUnsigned( Instruction instr ) noexcept
{
	const bool set = m_registers[ instr.rs() ] < m_registers[ instr.rt() ];
	m_registers.Set( instr.rd(), static_cast<uint32_t>( set ) );
}

void MipsR3000Cpu::ShiftRightArithmetic( Instruction instr ) noexcept
{
	// TODO: check compiler for arithemtic shift right
	m_registers.Set( instr.rd(), static_cast<int32_t>( m_registers[ instr.rt() ] ) >> instr.shamt() );
}

void MipsR3000Cpu::ShiftRightArithmeticVariable( Instruction instr ) noexcept
{
	// TODO: check compiler for arithemtic shift right
	m_registers.Set( instr.rd(), static_cast<int32_t>( m_registers[ instr.rt() ] ) >> ( m_registers[ instr.rs() ] & 0x1f ) );
}

void MipsR3000Cpu::ShiftRightLogical( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rt() ] >> instr.shamt() );
}

void MipsR3000Cpu::ShiftRightLogicalVariable( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rt() ] >> ( m_registers[ instr.rs() ] & 0x1f ) );
}

void MipsR3000Cpu::Subtract( Instruction instr ) noexcept
{
	SubtractTrap( m_registers[ instr.rs() ], m_registers[ instr.rt() ], m_registers[ instr.rd() ] );
}

void MipsR3000Cpu::SubtractUnsigned( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] - m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::StoreWord( Instruction instr ) noexcept
{
	StoreImp<uint32_t>( instr );
}

void MipsR3000Cpu::StoreWordFromCoprocessor( Instruction instr ) noexcept
{
	dbExpects( instr.z() != 0 ); // instruction is invalid for coprocessor 0

	const uint32_t address = GetVAddr( instr );
	dbExpects( address % 4 == 0 ); // exception for non word-aligned address

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
		StoreImp<uint8_t>( addr++, *( regByte++ ) );
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
		StoreImp<uint8_t>( addr--, *( regByte-- ) );
	}
	while ( addr % 4 != 3 );
}

void MipsR3000Cpu::SystemCall( Instruction ) noexcept
{
	RaiseException( Cop0::ExceptionCode::Syscall );
}

void MipsR3000Cpu::BitwiseXor( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] ^ m_registers[ instr.rt() ] );
}

void MipsR3000Cpu::BitwiseXorImmediate( Instruction instr ) noexcept
{
	m_registers.Set( instr.rt(), m_registers[ instr.rs() ] ^ instr.immediateUnsigned() );
}

void MipsR3000Cpu::IllegalInstruction( [[maybe_unused]] Instruction instr ) noexcept
{
	dbBreakMessage( "Illegal instruction [%X]", instr.value );
	RaiseException( Cop0::ExceptionCode::ReservedInstruction );
}

}