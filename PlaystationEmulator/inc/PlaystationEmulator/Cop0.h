#pragma once

#include "assert.h"

#include <array>
#include <cstdint>

namespace PSX
{

class Cop0
{
public:
	struct Register
	{
		enum : size_t
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
	};

	struct SystemStatus
	{
		enum : uint32_t
		{
			InterruptEnable = 0x00000001,
			UserMode = 0x00000002,
			PreviousInterruptDisable = 0x00000004,
			PreviousUserMode = 0x00000008,
			OldInterruptDisable = 0x00000010,
			OldUserMode = 0x00000020,

			IsolateCache = 0x00010000,
			SwappedCachemode = 0x00020000,
			PZ = 0x00040000,
			CM = 0x00080000,
			CacheParityError = 0x00100000,
			TlbShutdown = 0x00200000,
			BootExceptionVector = 0x00400000,
			ReverseEndianess = 0x02000000,
			Cop0Enable = 0x10000000,
			Cop1Enable = 0x20000000,
			Cop2Enable = 0x40000000,
			Cop3Enable = 0x80000000,
		};
	};

	enum class ExceptionCause : uint32_t
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

	static constexpr uint32_t CauseWriteBits = 0x00000300;

	void Reset()
	{
		m_registers.fill( 0 );
	}

	uint32_t Read( uint32_t index ) const noexcept { return m_registers[ index ]; }

	void Write( uint32_t index, uint32_t value ) noexcept
	{
		dbExpects( index < m_registers.size() );

		if ( ( WriteableRegistersMask & ( 1u << index ) ) == 0 )
			return;

		switch ( index )
		{
			case Register::ExceptionCause:
				value = ( value & CauseWriteBits ) | ( m_registers[ index ] & ~CauseWriteBits );
				break;
		}

		m_registers[ index ] = value;
	}

	bool GetIsolateCache() const noexcept { return ( m_registers[ Register::SystemStatus ] & SystemStatus::IsolateCache ) != 0; }

	void SetException( uint32_t pc, ExceptionCause cause, uint32_t coprocessor = 0, bool branch = false )
	{
		m_registers[ Register::TrapReturnAddress ] = pc;
		m_registers[ Register::ExceptionCause ] = ( static_cast<uint32_t>( cause ) << 2 ) | ( coprocessor << 28 ) | ( static_cast<uint32_t>( branch ) << 31 );

		// save interrupt enable and user/kernel mode
		auto& sr = m_registers[ Register::SystemStatus ];
		sr = ( ( sr & 0x0000000f ) << 2 ) | ( sr & 0xffffffc0 );
	}

	uint32_t GetExceptionVector() const noexcept
	{
		return ( m_registers[ Register::SystemStatus ] & SystemStatus::BootExceptionVector ) ? 0xbfc00180 : 0x80000080;
	}

	void PrepareReturnFromException() noexcept
	{
		// restore interrupt enable and user/kernel mode
		auto& sr = m_registers[ Register::SystemStatus ];
		sr = ( ( sr >> 2 ) & 0x0000000f ) | ( sr & 0xfffffff0 );
	}

private:

	static constexpr uint32_t WriteableRegistersMask = 0b0001101010101000;

	std::array<uint32_t, 16> m_registers{};
};

}