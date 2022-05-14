#include "SerialPort.h"

#include "SaveState.h"

namespace PSX
{

void SerialPort::Reset()
{
	m_status.value = 0;
	m_mode.value = 0;
	m_control.value = 0;
	m_misc = 0;
	m_baudrateReloadValue = DefaultBadrateReloadValue;
}

uint32_t SerialPort::ReadData() noexcept
{
	dbLogDebug( "SerialPort::Read -- data" );
	return 0xffffffff;
}

void SerialPort::WriteData( uint32_t value ) noexcept
{
	dbLogDebug( "SerialPort::Write -- data [%01X]", value );

	// TODO: start transfer
	(void)value;
}

void SerialPort::WriteControl( uint16_t value ) noexcept
{
	dbLogDebug( "SerialPort::Write -- control [%01X]", value );
	m_control.value = value & Control::WriteMask;

	if ( m_control.acknowledge )
	{
		m_status.rxParityError = false;
		m_status.rxFifoOverrun = false;
		m_status.rxBadStopBit = false;
		m_status.interruptRequest = false;
	}

	if ( m_control.reset )
	{
		// soft reset
		m_control.value = 0;
		m_status.value = 0;
		m_mode.value = 0;
		m_baudrateReloadValue = DefaultBadrateReloadValue;

		// Duckstation sets transfer to started and finished
		m_status.txReadyStarted = true;
		m_status.txReadyFinished = true;
	}
}

void SerialPort::Serialize( SaveStateSerializer& serializer )
{
	if ( !serializer.Header( "SerialPort", 1 ) )
		return;

	serializer( m_status.value );
	serializer( m_mode.value );
	serializer( m_control.value );
	serializer( m_misc);
	serializer( m_baudrateReloadValue );
}

}