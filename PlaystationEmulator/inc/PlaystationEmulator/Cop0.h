#pragma once

#include "assert.h"

#include <array>
#include <cstdint>

namespace PSX
{

class Cop0
{
public:
	enum class Register : uint32_t
	{
		BreakpointOnExecute = 3,
		BreakpointOnDataAccess = 5,
		JumpDestination = 6,
		BreakpointControl = 7,
		BadVirtualAddress = 8,
		DataAccessBreakpointMask = 9,
		ExecuteBreakpointMask = 11,
		SystemStatus = 12,
		ExceptionCause = 13,
		TrapReturnAddress = 14, // EPC
		ProcessorId = 15
	};

	struct ExceptionCause
	{
		enum : uint32_t
		{
			ExceptionCodeMask = 0x1f << 2,
			InterruptPendingMask = 0xff << 8,
			CoprocessorMask = 0x3 << 28,
			BranchDelay = 1u << 31,

			WriteMask = 0x3 << 8,
		};
	};

	enum class ExceptionCode : uint32_t
	{
		Interrupt,
		TlbModification,
		TlbStore,
		TlbLoad,
		AddressErrorLoad, // data load or instruction fetch
		AddressErrorStore, // data store
		BusErrorInstructionFetch,
		BusErrorDataLoadStore,
		Syscall,
		Breakpoint,
		ReservedInstruction,
		CoprocessorUnusable,
		ArithmeticOverflow
	};

	struct SystemStatus
	{
		enum : uint32_t
		{
			InterruptEnable = 1u << 0,
			UserMode = 1u << 1,
			PreviousInterruptDisable = 1u << 2,
			PreviousUserMode = 1u << 3,
			OldInterruptDisable = 1u << 4,
			OldUserMode = 1u << 5,
			InterruptMask = 0xffu << 8,
			IsolateCache = 1u << 16,
			SwappedCachemode = 1u << 17,
			PZ = 1u << 18,
			CM = 1u << 19,
			CacheParityError = 1u << 20,
			TlbShutdown = 1u << 21,
			BootExceptionVector = 1u << 22,
			ReverseEndianess = 1u << 25,
			Cop0Enable = 1u << 28,
			Cop1Enable = 1u << 29,
			Cop2Enable = 1u << 30,
			Cop3Enable = 1u << 31,

			WriteMask = InterruptEnable | UserMode | PreviousInterruptDisable | PreviousUserMode | OldInterruptDisable |
						OldUserMode | InterruptMask | IsolateCache | SwappedCachemode | PZ | CM | CacheParityError | TlbShutdown |
						BootExceptionVector | ReverseEndianess | Cop0Enable | Cop1Enable | Cop2Enable | Cop3Enable
		};
	};

	void Reset();

	uint32_t Read( uint32_t index ) const noexcept;

	void Write( uint32_t index, uint32_t value ) noexcept;

	bool GetIsolateCache() const noexcept
	{
		return m_systemStatus & SystemStatus::IsolateCache;
	}

	uint32_t GetExceptionVector() const noexcept
	{
		return ( m_systemStatus & SystemStatus::BootExceptionVector ) ? 0xbfc00180 : 0x80000080;
	}

	bool CheckException() const noexcept
	{
		return ( m_systemStatus & m_exceptionCause & SystemStatus::InterruptMask ) != 0;
	}

	void SetInterrupts( uint32_t interrupts ) noexcept
	{
		dbExpects( ( interrupts & ~ExceptionCause::InterruptPendingMask ) == 0 );
		m_exceptionCause |= interrupts;
	}

	void SetException( uint32_t pc, ExceptionCode code, uint32_t coprocessor = 0, bool branch = false ) noexcept;

	void PrepareReturnFromException() noexcept;

private:
	uint32_t m_breakpointOnExecute;
	uint32_t m_breakpointOnDataAccess;
	uint32_t m_jumpDestination;
	uint32_t m_breakpointControl;
	uint32_t m_badVirtualAddress;
	uint32_t m_dataAccessBreakpointMask;
	uint32_t m_executeBreakpointMask;
	uint32_t m_systemStatus;
	uint32_t m_exceptionCause;
	uint32_t m_trapReturnAddress;
	uint32_t m_processorId;
};

}