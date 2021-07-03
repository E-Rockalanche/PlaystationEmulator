#include "Cop0.h"

#include "InterruptControl.h"

#include <stdx/bit.h>

namespace PSX
{

void Cop0::Reset()
{
	m_breakpointOnExecute = 0;
	m_breakpointOnDataAccess = 0;
	m_jumpDestination = 0;
	m_breakpointControl = 0;
	m_badVirtualAddress = 0;
	m_dataAccessBreakpointMask = 0;
	m_executeBreakpointMask = 0;
	m_systemStatus = 0;
	m_exceptionCause = 0;
	m_trapReturnAddress = 0;
	m_processorId = 0;
}

uint32_t Cop0::Read( uint32_t index ) const noexcept
{
	dbExpects( index < 64 );
	switch ( static_cast<Register>( index ) )
	{
		case Register::BreakpointOnExecute:			return m_breakpointOnExecute;
		case Register::BreakpointOnDataAccess:		return m_breakpointOnDataAccess;
		case Register::JumpDestination:				return m_jumpDestination;
		case Register::BreakpointControl:			return m_breakpointControl;
		case Register::BadVirtualAddress:			return m_badVirtualAddress;
		case Register::DataAccessBreakpointMask:	return m_dataAccessBreakpointMask;
		case Register::ExecuteBreakpointMask:		return m_executeBreakpointMask;
		case Register::SystemStatus:				return m_systemStatus;
		case Register::ExceptionCause:				return GetExceptionCause();
		case Register::TrapReturnAddress:			return m_trapReturnAddress;
		case Register::ProcessorId:					return m_processorId;

		default:
			dbLog( "Cop0::Read -- reading garbage register [%u]", index );
			return 0;
	}
}

void Cop0::Write( uint32_t index, uint32_t value ) noexcept
{
	dbExpects( index < 64 );
	switch ( static_cast<Register>( index ) )
	{
		case Register::BreakpointOnExecute:
			m_breakpointOnExecute = value;
			break;

		case Register::BreakpointOnDataAccess:
			m_breakpointOnDataAccess = value;
			break;

		case Register::BreakpointControl:
			m_breakpointControl = value;
			break;

		case Register::DataAccessBreakpointMask:
			m_dataAccessBreakpointMask = value;
			break;

		case Register::ExecuteBreakpointMask:
			m_executeBreakpointMask = value;
			break;

		case Register::SystemStatus:
			stdx::masked_set<uint32_t>( m_systemStatus, SystemStatus::WriteMask, value );
			break;

		case Register::ExceptionCause:
			stdx::masked_set<uint32_t>( m_exceptionCause, ExceptionCause::WriteMask, value );
			break;

		default:
			dbLog( "Cop0::Write -- writing to read-only register [%u]", index );
			break;
	}
}

void Cop0::SetException( uint32_t pc, ExceptionCode code, uint32_t coprocessor, bool branchDelay ) noexcept
{
	dbExpects( coprocessor < 4 );

	dbLog( "Cop0::SetException() -- pc: %u, code: %u, coprocessor?: %u, branchDelay: %s", pc, static_cast<uint32_t>( code ), coprocessor, branchDelay ? "true" : "false" );

	m_trapReturnAddress = pc;
	m_exceptionCause = ( static_cast<uint32_t>( code ) << 2 ) | ( coprocessor << 28 ) | ( static_cast<uint32_t>( branchDelay ) << 31 );

	// save interrupt enable and user/kernel mode
	m_systemStatus = ( ( m_systemStatus & 0x0000000fu ) << 2 ) | ( m_systemStatus & 0xffffffc0u );

	// TODO: set jump destination?
}

void Cop0::PrepareReturnFromException() noexcept
{
	dbLog( "Cop0::PrepareReturnFromException()" );

	// restore interrupt enable and user/kernel mode
	m_systemStatus = ( ( m_systemStatus >> 2 ) & 0x0000000fu ) | ( m_systemStatus & 0xfffffff0u );
}

}