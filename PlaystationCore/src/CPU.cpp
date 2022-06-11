#include "CPU.h"

#include "BIOS.h"
#include "EventManager.h"
#include "File.h"
#include "MemoryMap.h"
#include "SaveState.h"

#include <stdx/assert.h>

#include <fstream>
#include <type_traits>

namespace PSX
{

void MipsR3000Cpu::Reset()
{
	m_currentPC = 0;
	SetProgramCounter( ResetVector );

	m_inBranch = false;
	m_inDelaySlot = false;

	m_registers.Reset();

	m_lo = 0;
	m_hi = 0;

	m_cop0.Reset();
	m_gte.Reset();
}

void MipsR3000Cpu::RunUntilEvent() noexcept
{
	while ( !m_eventManager.ReadyForNextEvent() )
	{

#ifdef PSX_HOOK_EXE
		if ( !m_exeFilename.empty() && m_pc == HookAddress )
		{
			LoadExecutable( m_exeFilename, *this, m_memoryMap.GetRam() );
			m_exeFilename.clear();
		}
#endif

		// the MIPS cpu is pipelined. The next instruction is fetched while the current one executes
		// this causes instructions after branches and jumps to always be executed

		m_inDelaySlot = m_inBranch;
		m_inBranch = false;

		if ( STDX_unlikely( m_cop0.ShouldTriggerInterrupt() ) )
		{
			// the exception handler may or may not modify the return address if the next instruction is a GTE command
			// this usually results in polygon flickering
			// to prevent this, delay the interrupt until after the GTE command
			const auto instruction = m_memoryMap.FetchInstruction( m_pc );
			if ( instruction.has_value() && ( instruction->value & 0xfe000000 ) != 0x4a000000 )
			{
				// update current PC now so we can save the proper return address
				m_currentPC = m_pc;
				RaiseException( Cop0::ExceptionCode::Interrupt );
			}
		}

		m_currentPC = m_pc;
		m_pc = m_nextPC;
		m_nextPC += 4;

		// CPU is pipelined so that each instruction takes 1 cycle
		m_eventManager.AddCycles( 1 );
		
#ifdef PSX_HOOK_BIOS
		if ( EnableBiosIntercept )
			InterceptBios( m_currentPC );
#endif

		const auto instruction = m_memoryMap.FetchInstruction( m_currentPC );
		if ( instruction.has_value() )
		{
			ExecuteInstruction( *instruction );

			m_registers.Update();
		}
		else
		{
			RaiseException( Cop0::ExceptionCode::AddressErrorLoad );
		}
	}

	m_eventManager.UpdateNextEvent();
}


inline void MipsR3000Cpu::InterceptBios( uint32_t pc )
{
	pc &= 0x1fffffff;

	const auto call = m_registers[ 9 ];
	if ( EnableKernelLogging )
	{
		const auto retAddr = m_registers[ Registers::ReturnAddress ] - 8;
		switch ( pc )
		{
			case 0xa0:	LogKernalCallA( call, retAddr );	break;
			case 0xb0:	LogKernalCallB( call, retAddr );	break;
			case 0xc0:	LogKernalCallC( call, retAddr );	break;
		}
	}

	auto LogCharacter = [&]( char c )
	{
		m_consoleOutput += c;
		if ( c == '\n' )
		{
			std::printf( m_consoleOutput.c_str() );
			m_consoleOutput.clear();
		}
	};

	auto FilePutC = [&]
	{
		const auto c = static_cast<char>( m_registers[ Registers::Arg0 ] );
		// const auto fd = m_registers[ Registers::Arg1 ];
		LogCharacter( c );
	};

	auto PutChar = [&]
	{
		LogCharacter( static_cast<char>( m_registers[ Registers::Arg0 ] ) );
	};

	auto PutS = [&]
	{
		const char* str = (const char*)m_memoryMap.GetRealAddress( m_registers[ Registers::Arg0 ] );
		while ( *str != '\0' )
			LogCharacter( *str++ );

		LogCharacter( '\n' );
	};

	if ( pc == 0xa0 )
	{
		switch ( call )
		{
			case 0x09:	FilePutC();		break;
			case 0x3c:	PutChar();		break;
			case 0x3e:	PutS();			break;
		}
	}
	else if ( pc == 0xb0 )
	{
		switch ( call )
		{
			case 0x3b:	FilePutC();		break;
			case 0x3d:	PutChar();		break;
			case 0x3f:	PutS();			break;
		}
	}
}

inline void MipsR3000Cpu::ExecuteInstruction( Instruction instr ) noexcept
{
	if ( EnableCpuLogging )
	{
		std::printf( "pc(%08X): ", m_currentPC );
		PrintDisassembly( instr );
	}

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

void MipsR3000Cpu::RaiseException( Cop0::ExceptionCode code, uint32_t coprocessor ) noexcept
{
	const uint32_t returnAddress = m_inDelaySlot ? m_currentPC - 4 : m_currentPC;
	m_cop0.SetException( returnAddress, code, coprocessor, m_inDelaySlot );
	SetProgramCounter( m_cop0.GetExceptionVector() );
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

inline void MipsR3000Cpu::Special( Instruction instr ) noexcept
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

inline void MipsR3000Cpu::RegisterImmediate( Instruction instr ) noexcept
{
	auto opcode = instr.rt();
	opcode = ( ( ( opcode & 0x1e ) == 0x10 ) ? 0x10 : 0 ) | ( opcode & 1 );
	switch ( static_cast<RegImmOpcode>( opcode ) )
	{
		case RegImmOpcode::BranchLessThanZero:
			BranchLessThanZero( instr );
			break;

		case RegImmOpcode::BranchGreaterEqualZero:
			BranchGreaterEqualZero( instr );
			break;

		case RegImmOpcode::BranchLessThanZeroAndLink:
			BranchLessThanZeroAndLink( instr );
			break;

		case RegImmOpcode::BranchGreaterEqualZeroAndLink:
			BranchGreaterEqualZeroAndLink( instr );
			break;

		default:
			dbBreak();
			break;
	}
}

inline void MipsR3000Cpu::CoprocessorUnit( Instruction instr ) noexcept
{
	const uint32_t coprocessor = instr.z();
	if ( !m_cop0.IsCoprocessorEnabled( coprocessor ) )
	{
		RaiseException( Cop0::ExceptionCode::CoprocessorUnusable, coprocessor );
		return;
	}

	if ( coprocessor == 2 )
		m_eventManager.StallUntilGteComplete();

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
			if ( STDX_likely( instr.subop() & 0b10000 ) )
				CoprocessorOperation( instr );
			else
				IllegalInstruction( instr );

			break;
		}
	}
}

inline void MipsR3000Cpu::Add( Instruction instr ) noexcept
{
	AddTrap( m_registers[ instr.rs() ], m_registers[ instr.rt() ], instr.rd() );
}

inline void MipsR3000Cpu::AddImmediate( Instruction instr ) noexcept
{
	AddTrap( m_registers[ instr.rs() ], instr.immediateSignExtended(), instr.rt() );
}

inline void MipsR3000Cpu::AddImmediateUnsigned( Instruction instr ) noexcept
{
	m_registers.Set( instr.rt(), m_registers[ instr.rs() ] + instr.immediateSignExtended() );
}

inline void MipsR3000Cpu::AddUnsigned( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] + m_registers[ instr.rt() ] );
}

inline void MipsR3000Cpu::BitwiseAnd( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] & m_registers[ instr.rt() ] );
}

inline void MipsR3000Cpu::BitwiseAndImmediate( Instruction instr ) noexcept
{
	m_registers.Set( instr.rt(), m_registers[ instr.rs() ] & instr.immediateUnsigned() );
}

inline void MipsR3000Cpu::BranchEqual( Instruction instr ) noexcept
{
	BranchImp( m_registers[ instr.rs() ] == m_registers[ instr.rt() ], instr.offset() );
}

inline void MipsR3000Cpu::BranchGreaterEqualZero( Instruction instr ) noexcept
{
	BranchImp( static_cast<int32_t>( m_registers[ instr.rs() ] ) >= 0, instr.offset() );
}

inline void MipsR3000Cpu::BranchGreaterEqualZeroAndLink( Instruction instr ) noexcept
{
	BranchImp( static_cast<int32_t>( m_registers[ instr.rs() ] ) >= 0, instr.offset() );

	// store return address after delay slot
	m_registers.Set( Registers::ReturnAddress, m_currentPC + 8 );
}

inline void MipsR3000Cpu::BranchGreaterThanZero( Instruction instr ) noexcept
{
	BranchImp( static_cast<int32_t>( m_registers[ instr.rs() ] ) > 0, instr.offset() );
}

inline void MipsR3000Cpu::BranchLessEqualZero( Instruction instr ) noexcept
{
	BranchImp( static_cast<int32_t>( m_registers[ instr.rs() ] ) <= 0, instr.offset() );
}

inline void MipsR3000Cpu::BranchLessThanZero( Instruction instr ) noexcept
{
	BranchImp( static_cast<int32_t>( m_registers[ instr.rs() ] ) < 0, instr.offset() );
}

inline void MipsR3000Cpu::BranchLessThanZeroAndLink( Instruction instr ) noexcept
{
	// R31 should not be used as the branch address

	BranchImp( static_cast<int32_t>( m_registers[ instr.rs() ] ) < 0, instr.offset() );

	// store return address after delay slot
	m_registers.Set( Registers::ReturnAddress, m_currentPC + 8 );
}

inline void MipsR3000Cpu::BranchNotEqual( Instruction instr ) noexcept
{
	BranchImp( m_registers[ instr.rs() ] != m_registers[ instr.rt() ], instr.offset() );
}

inline void MipsR3000Cpu::Break( Instruction ) noexcept
{
	RaiseException( Cop0::ExceptionCode::Breakpoint );
}

inline void MipsR3000Cpu::MoveControlFromCoprocessor( Instruction instr ) noexcept
{
	if ( instr.z() == 2 )
		m_registers.Load( instr.rt(), m_gte.ReadControl( instr.rd() ) );
}

inline void MipsR3000Cpu::CoprocessorOperation( Instruction instr ) noexcept
{
	switch ( instr.z() )
	{
		case 0:
			m_cop0.PrepareReturnFromException();
			break;

		case 2:
		{
			const cycles_t gteCommandCycles = m_gte.ExecuteCommand( instr.value );
			m_eventManager.AddGteCycles( gteCommandCycles );
			break;
		}
	}
}

inline void MipsR3000Cpu::MoveControlToCoprocessor( Instruction instr ) noexcept
{
	if ( instr.z() == 2 )
		m_gte.WriteControl( instr.rd(), m_registers[ instr.rt() ] );
}

inline void MipsR3000Cpu::Divide( Instruction instr ) noexcept
{
	const int32_t x = static_cast<int32_t>( m_registers[ instr.rs() ] );
	const int32_t y = static_cast<int32_t>( m_registers[ instr.rt() ] );

	if ( y == 0 )
	{
		// divide by zero
		m_lo = ( x >= 0 ) ? 0xffffffffu : 1u;
		m_hi = static_cast<uint32_t>( x );
	}
	else if ( y == -1 && x == std::numeric_limits<int32_t>::min() )
	{
		// integer overflow (-INT_MIN does not fit in s32)
		m_lo = 0x80000000;
		m_hi = 0;
	}
	else
	{
		m_lo = static_cast<uint32_t>( x / y );
		m_hi = static_cast<uint32_t>( x % y );
	}
}

inline void MipsR3000Cpu::DivideUnsigned( Instruction instr ) noexcept
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

inline void MipsR3000Cpu::Jump( Instruction instr ) noexcept
{
	JumpImp( instr.target() );
}

inline void MipsR3000Cpu::JumpAndLink( Instruction instr ) noexcept
{
	JumpImp( instr.target() );
	// store return address after delay slot
	// PC is already after delay slot
	m_registers.Set( Registers::ReturnAddress, m_currentPC + 8 );
}

inline void MipsR3000Cpu::JumpAndLinkRegister( Instruction instr ) noexcept
{
	// store return address after delay slot
	// PC is already after delay slot
	m_inBranch = true;
	m_nextPC = m_registers[ instr.rs() ];
	m_registers.Set( instr.rd(), m_currentPC + 8 );

	CheckProgramCounterAlignment();
}

inline void MipsR3000Cpu::JumpRegister( Instruction instr ) noexcept
{
	m_inBranch = true;
	m_nextPC = m_registers[ instr.rs() ];

	CheckProgramCounterAlignment();
}

inline void MipsR3000Cpu::LoadByte( Instruction instr ) noexcept
{
	LoadImp<int8_t>( instr );
}

inline void MipsR3000Cpu::LoadByteUnsigned( Instruction instr ) noexcept
{
	LoadImp<uint8_t>( instr );
}

inline void MipsR3000Cpu::LoadHalfword( Instruction instr ) noexcept
{
	LoadImp<int16_t>( instr );
}

inline void MipsR3000Cpu::LoadHalfwordUnsigned( Instruction instr ) noexcept
{
	LoadImp<uint16_t>( instr );
}

inline void MipsR3000Cpu::LoadUpperImmediate( Instruction instr ) noexcept
{
	m_registers.Set( instr.rt(), static_cast<uint32_t>( instr.immediateUnsigned() ) << 16 );
}

inline void MipsR3000Cpu::LoadWord( Instruction instr ) noexcept
{
	LoadImp<int32_t>( instr );
}

inline void MipsR3000Cpu::LoadWordToCoprocessor( Instruction instr ) noexcept
{
	const uint32_t coprocessor = instr.z();
	if ( !m_cop0.IsCoprocessorEnabled( coprocessor ) )
	{
		RaiseException( Cop0::ExceptionCode::CoprocessorUnusable, coprocessor );
		return;
	}

	const uint32_t address = GetVAddr( instr );
	if ( address % 4 != 0 )
	{
		RaiseException( Cop0::ExceptionCode::AddressErrorLoad );
		return;
	}

	if ( coprocessor == 2 )
	{
		m_eventManager.StallUntilGteComplete();
		m_gte.Write( instr.rt(), LoadImp<uint32_t>( address ) );
	}
}

inline void MipsR3000Cpu::LoadWordLeft( Instruction instr ) noexcept
{
	// load high bytes of word crossing word boundary

	uint32_t addr = GetVAddr( instr );

	union
	{
		uint32_t reg;
		uint8_t regBytes[ 4 ];
	};

	if ( m_registers.GetLoadDelayIndex() == instr.rt() )
		reg = m_registers.GetLoadDelayValue();
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

inline void MipsR3000Cpu::LoadWordRight( Instruction instr ) noexcept
{
	// load low bytes of word crossing word boundary

	uint32_t addr = GetVAddr( instr );

	union
	{
		uint32_t reg;
		uint8_t regBytes[ 4 ];
	};

	if ( m_registers.GetLoadDelayIndex() == instr.rt() )
		reg = m_registers.GetLoadDelayValue();
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

inline void MipsR3000Cpu::MoveFromCoprocessor( Instruction instr ) noexcept
{
	const uint32_t rt = instr.rt();
	const uint32_t rd = instr.rd();
	switch ( instr.z() )
	{
		case 0:
			m_registers.Load( rt, m_cop0.Read( rd ) );
			break;

		case 2:
			m_registers.Load( rt, m_gte.Read( rd ) );
			break;
	}
}

inline void MipsR3000Cpu::MoveFromHi( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_hi );
}

inline void MipsR3000Cpu::MoveFromLo( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_lo );
}

inline void MipsR3000Cpu::MoveToCoprocessor( Instruction instr ) noexcept
{
	const uint32_t rd = instr.rd();
	const uint32_t value = m_registers[ instr.rt() ];
	switch ( instr.z() )
	{
		case 0:
			m_cop0.Write( rd, value );
			break;

		case 2:
			m_gte.Write( rd, value );
			break;
	}
}

inline void MipsR3000Cpu::MoveToHi( Instruction instr ) noexcept
{
	m_hi = m_registers[ instr.rs() ];
}

inline void MipsR3000Cpu::MoveToLo( Instruction instr ) noexcept
{
	m_lo = m_registers[ instr.rs() ];
}

inline void MipsR3000Cpu::Multiply( Instruction instr ) noexcept
{
	const auto x = static_cast<int64_t>( static_cast<int32_t>( m_registers[ instr.rs() ] ) );
	const auto y = static_cast<int64_t>( static_cast<int32_t>( m_registers[ instr.rt() ] ) );

	const int64_t result = x * y;

	m_lo = static_cast<uint32_t>( result );
	m_hi = static_cast<uint32_t>( result >> 32 );
}

inline void MipsR3000Cpu::MultiplyUnsigned( Instruction instr ) noexcept
{
	const auto x = static_cast<uint64_t>( m_registers[ instr.rs() ] );
	const auto y = static_cast<uint64_t>( m_registers[ instr.rt() ] );

	const uint64_t result = x * y;

	m_lo = static_cast<uint32_t>( result );
	m_hi = static_cast<uint32_t>( result >> 32 );
}

inline void MipsR3000Cpu::BitwiseNor( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), ~( m_registers[ instr.rs() ] | m_registers[ instr.rt() ] ) );
}

inline void MipsR3000Cpu::BitwiseOr( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] | m_registers[ instr.rt() ] );
}

inline void MipsR3000Cpu::BitwiseOrImmediate( Instruction instr ) noexcept
{
	m_registers.Set( instr.rt(), m_registers[ instr.rs() ] | instr.immediateUnsigned() );
}

inline void MipsR3000Cpu::StoreByte( Instruction instr ) noexcept
{
	StoreImp<uint8_t>( instr );
}

inline void MipsR3000Cpu::StoreHalfword( Instruction instr ) noexcept
{
	StoreImp<uint16_t>( instr );
}

inline void MipsR3000Cpu::ShiftLeftLogical( Instruction instr ) noexcept
{
	// 0 is a common NOP instruction (shift zero register by 0)
	if ( instr.value != 0 )
		m_registers.Set( instr.rd(), m_registers[ instr.rt() ] << instr.shamt() );
}

inline void MipsR3000Cpu::ShiftLeftLogicalVariable( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rt() ] << ( m_registers[ instr.rs() ] & 0x1f ) );
}

inline void MipsR3000Cpu::SetLessThan( Instruction instr ) noexcept
{
	const bool set = static_cast<int32_t>( m_registers[ instr.rs() ] ) < static_cast<int32_t>( m_registers[ instr.rt() ] );
	m_registers.Set( instr.rd(), static_cast<uint32_t>( set ) );
}

inline void MipsR3000Cpu::SetLessThanImmediate( Instruction instr ) noexcept
{
	// immediate is sign extended and compared with rs. Both values are considered signed
	const bool set = static_cast<int32_t>( m_registers[ instr.rs() ] ) < instr.immediateSigned();
	m_registers.Set( instr.rt(), static_cast<uint32_t>( set ) );
}

inline void MipsR3000Cpu::SetLessThanImmediateUnsigned( Instruction instr ) noexcept
{
	// immediate is sign extended and compared with rs. Both values are considered unsigned
	const bool set = m_registers[ instr.rs() ] < instr.immediateSignExtended();
	m_registers.Set( instr.rt(), static_cast<uint32_t>( set ) );
}

inline void MipsR3000Cpu::SetLessThanUnsigned( Instruction instr ) noexcept
{
	const bool set = m_registers[ instr.rs() ] < m_registers[ instr.rt() ];
	m_registers.Set( instr.rd(), static_cast<uint32_t>( set ) );
}

inline void MipsR3000Cpu::ShiftRightArithmetic( Instruction instr ) noexcept
{
	// TODO: check compiler for arithemtic shift right
	m_registers.Set( instr.rd(), static_cast<int32_t>( m_registers[ instr.rt() ] ) >> instr.shamt() );
}

inline void MipsR3000Cpu::ShiftRightArithmeticVariable( Instruction instr ) noexcept
{
	// TODO: check compiler for arithemtic shift right
	m_registers.Set( instr.rd(), static_cast<int32_t>( m_registers[ instr.rt() ] ) >> ( m_registers[ instr.rs() ] & 0x1f ) );
}

inline void MipsR3000Cpu::ShiftRightLogical( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rt() ] >> instr.shamt() );
}

inline void MipsR3000Cpu::ShiftRightLogicalVariable( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rt() ] >> ( m_registers[ instr.rs() ] & 0x1f ) );
}

inline void MipsR3000Cpu::Subtract( Instruction instr ) noexcept
{
	SubtractTrap( m_registers[ instr.rs() ], m_registers[ instr.rt() ], instr.rd() );
}

inline void MipsR3000Cpu::SubtractUnsigned( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] - m_registers[ instr.rt() ] );
}

inline void MipsR3000Cpu::StoreWord( Instruction instr ) noexcept
{
	StoreImp<uint32_t>( instr );
}

inline void MipsR3000Cpu::StoreWordFromCoprocessor( Instruction instr ) noexcept
{
	const uint32_t coprocessor = instr.z();
	if ( !m_cop0.IsCoprocessorEnabled( coprocessor ) )
	{
		RaiseException( Cop0::ExceptionCode::CoprocessorUnusable, coprocessor );
		return;
	}

	const uint32_t address = GetVAddr( instr );
	if ( address % 4 != 0 )
	{
		RaiseException( Cop0::ExceptionCode::AddressErrorStore, coprocessor );
		return;
	}

	if ( coprocessor == 2 )
	{
		m_eventManager.StallUntilGteComplete();
		m_memoryMap.Write<uint32_t>( address, m_gte.Read( instr.rt() ) );
	}
}

inline void MipsR3000Cpu::StoreWordLeft( Instruction instr ) noexcept
{
	// store high bytes to word crossing word boundary

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

inline void MipsR3000Cpu::StoreWordRight( Instruction instr ) noexcept
{
	// store low bytes to word crossing word boundary

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

inline void MipsR3000Cpu::SystemCall( Instruction ) noexcept
{
	if ( EnableKernelLogging )
		LogSystemCall( m_registers[ Registers::Arg0 ], m_currentPC );

	RaiseException( Cop0::ExceptionCode::Syscall );
}

inline void MipsR3000Cpu::BitwiseXor( Instruction instr ) noexcept
{
	m_registers.Set( instr.rd(), m_registers[ instr.rs() ] ^ m_registers[ instr.rt() ] );
}

inline void MipsR3000Cpu::BitwiseXorImmediate( Instruction instr ) noexcept
{
	m_registers.Set( instr.rt(), m_registers[ instr.rs() ] ^ instr.immediateUnsigned() );
}

inline void MipsR3000Cpu::IllegalInstruction( [[maybe_unused]] Instruction instr ) noexcept
{
	dbBreakMessage( "Illegal instruction [%X]", instr.value );
	RaiseException( Cop0::ExceptionCode::ReservedInstruction );
}

void MipsR3000Cpu::Serialize( SaveStateSerializer& serializer )
{
	if ( !serializer.Header( "CPU", 1 ) )
		return;

	m_registers.Serialize( serializer );

	serializer( m_currentPC );
	serializer( m_pc );
	serializer( m_nextPC );

	serializer( m_inBranch );
	serializer( m_inDelaySlot );

	serializer( m_hi );
	serializer( m_lo );

	serializer( m_consoleOutput );

	m_cop0.Serialize( serializer );
	m_gte.Serialize( serializer );
}

void MipsR3000Cpu::Registers::Serialize( SaveStateSerializer& serializer )
{
	serializer( m_registers );

	serializer( m_loadDelay.index );
	serializer( m_loadDelay.value );

	serializer( m_newLoadDelay.index );
	serializer( m_newLoadDelay.value );
}

}