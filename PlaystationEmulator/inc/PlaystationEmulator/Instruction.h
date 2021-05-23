#pragma once

#include <cstdint>
#include <utility>

namespace PSX
{

struct Instruction
{
	Instruction() noexcept = default; // NOP (SSL by 0 into reg 0)

	explicit Instruction( uint32_t instr ) noexcept : value{ instr } {};

	uint32_t value = 0;

	uint32_t funct() const noexcept { return value & 0x0000003f; }

	uint32_t shamt() const noexcept { return ( value >> 6 ) & 0x0000001f; }

	uint32_t rd() const noexcept { return ( value >> 11 ) & 0x0000001f; }

	uint32_t rt() const noexcept { return ( value >> 16 ) & 0x0000001f; }

	uint32_t rs() const noexcept { return ( value >> 21 ) & 0x0000001f; }

	uint32_t op() const noexcept { return ( value >> 26 ); }

	uint32_t immediateSigned() const noexcept { return static_cast<uint32_t>( static_cast<int32_t>( static_cast<int16_t>( value ) ) ); }
	uint32_t immediateUnsigned() const noexcept { return value & 0x0000ffff; }

	uint32_t offset() const noexcept { return immediateSigned(); }

	uint32_t base() const noexcept { return ( value >> 21 ) & 0x0000001f; }

	uint32_t target() const noexcept { return ( value & 0x03ffffff ) << 2; }

	uint32_t subop() const noexcept { return ( value >> 21 ) & 0x0000001f; }

	uint32_t z() const noexcept { return ( value >> 26 ) & 0x00000003; }

	uint32_t code() const noexcept { return ( value >> 6 ) & 0x000fffff; }

	uint32_t cofun() const noexcept { return value & 0x001fffff; }
};

enum class Operands
{
	None,
	RsRtRd,
	RsRtImm,
	RsRtOff,
	RsOff,
	Code,
	RtRd,
	RsRt,
	Target,
	RsRd,
	Rs,
	BaseRtOff,
	RtImm,
	Rd,
	RtRdSa,
	ZCofun,
	ZRtRd,
	ZBaseRtOff
};

enum class Opcode : uint32_t
{
	Special = 0b000000,

	RegisterImmediate = 0b000001,

	AddImmediate = 0b001000,
	AddImmediateUnsigned = 0b001001,
	BitwiseAndImmediate = 0b001100,
	BranchEqual = 0b000100,
	BranchGreaterThanZero = 0b000111,
	BranchLessEqualZero = 0b000110,
	BranchNotEqual = 0b000101,
	Jump = 0b000010,
	JumpAndLink = 0b000011,
	LoadByte = 0b100000,
	LoadByteUnsigned = 0b100100,
	LoadHalfword = 0b100001,
	LoadHalfwordUnsigned = 0b100101,
	LoadUpperImmediate = 0b001111,
	LoadWord = 0b100011,
	LoadWordLeft = 0b100010,
	LoadWordRight = 0b100110,
	BitwiseOrImmediate = 0b001101,
	StoreByte = 0b101000,
	StoreHalfword = 0b101001,
	SetLessThanImmediate = 0b001010,
	SetLessThanImmediateUnsigned = 0b001011,
	StoreWord = 0b101011,
	StoreWordLeft = 0b101010,
	StoreWordRight = 0b101110,
	BitwiseXorImmediate = 0b001110,

	CoprocessorUnit0 = 0b010000,
	CoprocessorUnit1 = 0b010001,
	CoprocessorUnit2 = 0b010010,
	CoprocessorUnit3 = 0b010011,

	LoadWordToCoprocessor0 = 0b110000,
	LoadWordToCoprocessor1 = 0b110001,
	LoadWordToCoprocessor2 = 0b110010,
	LoadWordToCoprocessor3 = 0b110011,

	StoreWordFromCoprocessor0 = 0b111000,
	StoreWordFromCoprocessor1 = 0b111001,
	StoreWordFromCoprocessor2 = 0b111010,
	StoreWordFromCoprocessor3 = 0b111011,
};

enum class SpecialOpcode : uint32_t
{
	Add = 0b100000,
	AddUnsigned = 0b100001,
	BitwiseAnd = 0b100100,
	Break = 0b001101,
	Divide = 0b011010,
	DivideUnsigned = 0b011011,
	JumpAndLinkRegister = 0b001001,
	JumpRegister = 0b001000,
	MoveFromHi = 0b010000,
	MoveFromLo = 0b010010,
	MoveToHi = 0b010001,
	MoveToLo = 0b010011,
	Multiply = 0b011000,
	MultiplyUnsigned = 0b011001,
	BitwiseNor = 0b100111,
	BitwiseOr = 0b100101,
	ShiftLeftLogical = 0b000000,
	ShiftLeftLogicalVariable = 0b000100,
	SetLessThan = 0b101010,
	SetLessThanUnsigned = 0b101011,
	ShiftRightArithmetic = 0b000011,
	ShiftRightArithmeticVariable = 0b000111,
	ShiftRightLogical = 0b000010,
	ShiftRightLogicalVariable = 0b000110,
	Subtract = 0b100010,
	SubtractUnsigned = 0b100011,
	SystemCall = 0b001100,
	BitwiseXor = 0b100110,
};

// RegImm functions in rt
enum class RegImmOpcode : uint32_t
{
	BranchGreaterEqualZero = 0b00001,
	BranchGreaterEqualZeroAndLink = 0b10001,
	BranchLessThanZero = 0b00000,
	BranchLessThanZeroAndLink = 0b10000,
};

// coprocessor subop in rs
enum class CoprocessorOpcode : uint32_t
{
	MoveControlFromCoprocessor = 0b00010,
	// CoprocessorOperation = 0b1xxxx,
	MoveControlToCoprocessor = 0b00110,
	MoveFromCoprocessor = 0b00000,
	MoveToCoprocessor = 0b00100,
};

enum class Cop0Opcode
{
	RestoreFromException = 0b010000,
	TlbProbe = 0b001000,
	ReadIndexedTlbEntry = 0b000001,
	WriteIndexedTlbEntry = 0b000010,
	WriteRandomTlbEntry = 0b000110,
};

std::pair<const char*, Operands> GetInstructionDisplay( Instruction instruction ) noexcept;

}