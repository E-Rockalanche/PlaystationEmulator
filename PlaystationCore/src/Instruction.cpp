#include "Instruction.h"

#include <array>
#include <cstdio>

namespace PSX
{

#define DEF_OP( opcode, name, args ) case opcode: return { name, Operands::args }; break;

std::pair<const char*, Operands> GetInstructionDisplay( Instruction instruction ) noexcept
{
	switch ( static_cast<Opcode>( instruction.op() ) )
	{
		case Opcode::Special:
		{
			if ( instruction.value == 0 )
				return { "NOP", Operands::None };

			switch ( static_cast<SpecialOpcode>( instruction.funct() ) )
			{
				DEF_OP( SpecialOpcode::Add, "ADD", RsRtRd );
				DEF_OP( SpecialOpcode::AddUnsigned, "ADDU", RsRtRd );
				DEF_OP( SpecialOpcode::BitwiseAnd, "AND", RsRtRd );
				DEF_OP( SpecialOpcode::Break, "BREAK", Code );
				DEF_OP( SpecialOpcode::Divide, "DIV", RsRt );
				DEF_OP( SpecialOpcode::DivideUnsigned, "DIVU", RsRt );
				DEF_OP( SpecialOpcode::JumpAndLinkRegister, "JALR", RsRd );
				DEF_OP( SpecialOpcode::JumpRegister, "JR", Rs );
				DEF_OP( SpecialOpcode::MoveFromHi, "MFHI", Rd );
				DEF_OP( SpecialOpcode::MoveFromLo, "MFLO", Rd );
				DEF_OP( SpecialOpcode::MoveToHi, "MTHI", Rs );
				DEF_OP( SpecialOpcode::MoveToLo, "MTLO", Rs );
				DEF_OP( SpecialOpcode::Multiply, "MULT", RsRt );
				DEF_OP( SpecialOpcode::MultiplyUnsigned, "MULTU", RsRt );
				DEF_OP( SpecialOpcode::BitwiseNor, "NOR", RsRtRd );
				DEF_OP( SpecialOpcode::BitwiseOr, "OR", RsRtRd );
				DEF_OP( SpecialOpcode::ShiftLeftLogical, "SLL", RtRdSa );
				DEF_OP( SpecialOpcode::ShiftLeftLogicalVariable, "SLLV", RsRtRd );
				DEF_OP( SpecialOpcode::SetLessThan, "SLT", RsRtRd );
				DEF_OP( SpecialOpcode::SetLessThanUnsigned, "SLTU", RsRtRd );
				DEF_OP( SpecialOpcode::ShiftRightArithmetic, "SRA", RtRdSa );
				DEF_OP( SpecialOpcode::ShiftRightArithmeticVariable, "SRAV", RsRtRd );
				DEF_OP( SpecialOpcode::ShiftRightLogical, "SRL", RtRdSa );
				DEF_OP( SpecialOpcode::ShiftRightLogicalVariable, "SRLV", RsRtRd );
				DEF_OP( SpecialOpcode::Subtract, "SUB", RsRtRd );
				DEF_OP( SpecialOpcode::SubtractUnsigned, "SUBU", RsRtRd );
				DEF_OP( SpecialOpcode::SystemCall, "SYSCALL", Code );
				DEF_OP( SpecialOpcode::BitwiseXor, "XOR", RsRtRd );
			}
			break;
		}

		case Opcode::RegisterImmediate:
		{
			switch ( static_cast<RegImmOpcode>( instruction.rt() ) )
			{
				DEF_OP( RegImmOpcode::BranchGreaterEqualZero, "BGEZ", RsOff );
				DEF_OP( RegImmOpcode::BranchGreaterEqualZeroAndLink, "BGEZAL", RsOff );
				DEF_OP( RegImmOpcode::BranchLessThanZero, "BLTZ", RsOff );
				DEF_OP( RegImmOpcode::BranchLessThanZeroAndLink, "BLTZAL", RsOff );
			}
			break;
		}

		case Opcode::CoprocessorUnit0:
		case Opcode::CoprocessorUnit1:
		case Opcode::CoprocessorUnit2:
		case Opcode::CoprocessorUnit3:
		{
			if ( instruction.rs() & 0b10000 )
				return { "COPz", Operands::ZCofun };

			switch ( static_cast<CoprocessorOpcode>( instruction.rs() ) )
			{
				DEF_OP( CoprocessorOpcode::MoveControlFromCoprocessor, "CFCz", ZRtRd );
				DEF_OP( CoprocessorOpcode::MoveControlToCoprocessor, "CTCz", ZRtRd );
				DEF_OP( CoprocessorOpcode::MoveFromCoprocessor, "MFCz", ZRtRd );
				DEF_OP( CoprocessorOpcode::MoveToCoprocessor, "MTCz", ZRtRd );
			}
		}

		case Opcode::LoadWordToCoprocessor0:
		case Opcode::LoadWordToCoprocessor1:
		case Opcode::LoadWordToCoprocessor2:
		case Opcode::LoadWordToCoprocessor3:
			return { "LWCz", Operands::ZBaseRtOff };

		case Opcode::StoreWordFromCoprocessor0:
		case Opcode::StoreWordFromCoprocessor1:
		case Opcode::StoreWordFromCoprocessor2:
		case Opcode::StoreWordFromCoprocessor3:
			return { "SWCz", Operands::ZBaseRtOff };

		DEF_OP( Opcode::AddImmediate, "ADDI", RsRtImm );
		DEF_OP( Opcode::AddImmediateUnsigned, "ADDIU", RsRtImm );
		DEF_OP( Opcode::BitwiseAndImmediate, "ANDI", RsRtImm );
		DEF_OP( Opcode::BranchEqual, "BEQ", RsRtOff );
		DEF_OP( Opcode::BranchGreaterThanZero, "BGTZ", RsOff );
		DEF_OP( Opcode::BranchLessEqualZero, "BLEZ", RsOff );
		DEF_OP( Opcode::BranchNotEqual, "BNE", RsRtOff );
		DEF_OP( Opcode::Jump, "J", Target );
		DEF_OP( Opcode::JumpAndLink, "JAL", Target );
		DEF_OP( Opcode::LoadByte, "LB", BaseRtOff );
		DEF_OP( Opcode::LoadByteUnsigned, "LBU", BaseRtOff );
		DEF_OP( Opcode::LoadHalfword, "LH", BaseRtOff );
		DEF_OP( Opcode::LoadHalfwordUnsigned, "LHU", BaseRtOff );
		DEF_OP( Opcode::LoadUpperImmediate, "LUI", RtImm );
		DEF_OP( Opcode::LoadWord, "LW", BaseRtOff );
		DEF_OP( Opcode::LoadWordLeft, "LWL", BaseRtOff );
		DEF_OP( Opcode::LoadWordRight, "LWR", BaseRtOff );
		DEF_OP( Opcode::BitwiseOrImmediate, "ORI", RsRtImm );
		DEF_OP( Opcode::StoreByte, "SB", BaseRtOff );
		DEF_OP( Opcode::StoreHalfword, "SH", BaseRtOff );
		DEF_OP( Opcode::SetLessThanImmediate, "SLTI", RsRtImm );
		DEF_OP( Opcode::SetLessThanImmediateUnsigned, "SLTIU", RsRtImm );
		DEF_OP( Opcode::StoreWord, "SW", BaseRtOff );
		DEF_OP( Opcode::StoreWordLeft, "SWL", BaseRtOff );
		DEF_OP( Opcode::StoreWordRight, "SWR", BaseRtOff );
		DEF_OP( Opcode::BitwiseXorImmediate, "XORI", RsRtImm );
	}

	return { "INVALID", Operands::None };
}

#undef DEF_OP

void PrintDisassembly( Instruction instr )
{
	auto[ name, args ] = GetInstructionDisplay( instr );
	switch ( args )
	{
		case Operands::None:		std::printf( "%s\n", name );																								break;
		case Operands::RsRtRd:		std::printf( "%s\trs:%u\trt:%u\trd:%u\n", name, instr.rs(), instr.rt(), instr.rd() );										break;
		case Operands::RsRtImm:		std::printf( "%s\trs:%u\trt:%u\timm:%i\n", name, instr.rs(), instr.rt(), static_cast<int32_t>( instr.immediateSigned() ) );	break;
		case Operands::RsRtOff:		std::printf( "%s\trs:%u\trt:%u\toff:%i\n", name, instr.rs(), instr.rt(), static_cast<int32_t>( instr.offset() ) );			break;
		case Operands::RsOff:		std::printf( "%s\trs:%u\toff:%i\n", name, instr.rs(), static_cast<int32_t>( instr.offset() ) );								break;
		case Operands::Code:		std::printf( "%s\tcode:%u\n", name, instr.code() );																			break;
		case Operands::RtRd:		std::printf( "%s\trt:%u\trd:%u\n", name, instr.rt(), instr.rd() );															break;
		case Operands::RsRt:		std::printf( "%s\trs:%u\trt:%u\n", name, instr.rs(), instr.rt() );															break;
		case Operands::Target:		std::printf( "%s\ttarget:%X\n", name, instr.target() );																		break;
		case Operands::RsRd:		std::printf( "%s\trs:%u\trd:%u\n", name, instr.rs(), instr.rd() );															break;
		case Operands::Rs:			std::printf( "%s\trs:%u\n", name, instr.rs() );																				break;
		case Operands::BaseRtOff:	std::printf( "%s\tbase:%u\trt:%u\toff:%i\n", name, instr.base(), instr.rt(), static_cast<int32_t>( instr.offset() ) );		break;
		case Operands::RtImm:		std::printf( "%s\trt:%u\timm:%i\n", name, instr.rt(), static_cast<int32_t>( instr.immediateSigned() ) );					break;
		case Operands::Rd:			std::printf( "%s\trd:%u\n", name, instr.rd() );																				break;
		case Operands::RtRdSa:		std::printf( "%s\trs:%u\trd:%u\tsa:%u\n", name, instr.rt(), instr.rd(), instr.shamt() );									break;
		case Operands::ZCofun:		std::printf( "%s\tz:%u\tcofun:%u\n", name, instr.z(), instr.cofun() );														break;
		case Operands::ZRtRd:		std::printf( "%s\tz:%u\trt:%u\trd:%u\n", name, instr.z(), instr.rt(), instr.rd() );											break;
		case Operands::ZBaseRtOff:	std::printf( "%s\tz:%u\tbase:%u\trt:%u\toff:%u\n", name, instr.z(), instr.base(), instr.rt(), instr.offset() );				break;
		default:					std::printf( "ILLEGAL" );																									break;
	}
}

}