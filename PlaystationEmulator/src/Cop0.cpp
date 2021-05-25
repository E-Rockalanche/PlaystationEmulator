#include "Cop0.h"

namespace PSX
{

void Cop0::Reset()
{
	m_registers.fill( 0 );
}

void Cop0::Write( uint32_t index, uint32_t value ) noexcept
{
	dbExpects( index < m_registers.size() );

	if ( ( WriteableRegistersMask & ( 1u << index ) ) == 0 )
		return;

	switch ( index )
	{
		case Register::SystemStatus:
			value &= SystemStatus::WriteMask;
			break;

		case Register::ExceptionCause:
			value = ( value & CauseWriteBits ) | ( m_registers[ index ] & ~CauseWriteBits );
			break;
	}

	m_registers[ index ] = value;
}

void Cop0::SetException( uint32_t pc, ExceptionCause cause, uint32_t coprocessor, bool branch ) noexcept
{
	m_registers[ Register::TrapReturnAddress ] = pc;
	m_registers[ Register::ExceptionCause ] = ( static_cast<uint32_t>( cause ) << 2 ) | ( coprocessor << 28 ) | ( static_cast<uint32_t>( branch ) << 31 );

	// save interrupt enable and user/kernel mode
	auto& sr = m_registers[ Register::SystemStatus ];
	sr = ( ( sr & 0x0000000f ) << 2 ) | ( sr & 0xffffffc0 );
}

void Cop0::PrepareReturnFromException() noexcept
{
	// restore interrupt enable and user/kernel mode
	auto& sr = m_registers[ Register::SystemStatus ];
	sr = ( ( sr >> 2 ) & 0x0000000f ) | ( sr & 0xfffffff0 );
}

}