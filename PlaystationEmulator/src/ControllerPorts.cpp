#include "ControllerPorts.h"

#include "Controller.h"
#include "EventManager.h"
#include "InterruptControl.h"

namespace PSX
{

ControllerPorts::ControllerPorts( InterruptControl& interruptControl, EventManager& eventManager )
	: m_interruptControl{ interruptControl }
{
	m_communicateEvent = eventManager.CreateEvent( "ControllerPorts communicate event", [this]( cycles_t cycles ) { UpdateCycles( cycles ); } );
}

void ControllerPorts::Reset()
{
	m_status = 0;
	m_baudrateTimer = 0;

	m_mode.value = 0;
	m_mode.baudrateReloadFactor = 1;
	// TODO: mode defaults to 8bit and no parity?

	m_control = 0;
	m_baudrateReloadValue = 0x0088;

	m_state = State::Idle;
	m_cyclesUntilEvent = 0;

	m_txBuffer = 0;
	m_txBufferFull = false;

	m_rxBuffer = 0;
	m_rxBufferFull = false;

	for ( auto* controller : m_controllers )
		if ( controller )
			controller->Reset();
}

uint32_t ControllerPorts::ReadData() noexcept
{
	// A data byte can be read when JOY_STAT.1=1. Data should be read only via 8bit memory access
	// (the 16bit/32bit "preview" feature is rather unusable, and usually there shouldn't be more than 1 byte in the FIFO anyways).

	const uint8_t data = m_rxBufferFull ? m_rxBuffer : 0xff;
	m_rxBufferFull = false;

	dbLog( "ControllerPorts::Read() -- data [%X]", data );
	return data | ( data << 8 ) | ( data << 16 ) | ( data << 24 );
}

uint32_t ControllerPorts::ReadStatus() noexcept
{
	const uint32_t status = m_status |
		( m_baudrateTimer << 11 ) |
		( m_rxBufferFull << 1 ) |
		( (uint32_t)!m_txBufferFull ) |
		( (uint32_t)IsFinishedTransfer() << 2 );

	// no logging since status is read a LOT
	// dbLog( "ControllerPorts::Read() -- status [%X]", status );

	// just reset the ack level since we don't emulate the timing for it
	stdx::reset_bits<uint32_t>( m_status, Status::AckInputLevel );

	return status;
}

void ControllerPorts::WriteData( uint32_t value ) noexcept
{
	m_communicateEvent->UpdateEarly();

	// Writing to this register starts the transfer (if, or as soon as TXEN=1 and JOY_STAT.2=Ready),
	// the written value is sent to the controller or memory card, and, simultaneously,
	// a byte is received (and stored in RX FIFO if JOY_CTRL.1 or JOY_CTRL.2 is set).
	dbLog( "ControllerPorts::Write() -- data [%X]", value );

	if ( m_txBufferFull )
		dbLogWarning( "ControllerPorts::WriteData() -- TX buffer is full" );

	m_txBuffer = static_cast<uint8_t>( value );
	m_txBufferFull = true;

	TryTransfer();
}

void ControllerPorts::WriteControl( uint16_t value ) noexcept
{
	m_communicateEvent->UpdateEarly();

	dbLog( "ControllerPorts::Write() -- control [%X]", value );
	m_control = value & Control::WriteMask;

	if ( value & Control::Reset )
	{
		// soft reset
		dbLog( "\tsoft reset" );
		m_control = 0;
		m_status = 0;
		m_mode.value = 0;
	}

	if ( value & Control::Acknowledge )
	{
		dbLog( "\tacknowledge" );
		stdx::reset_bits( m_status, Status::RxParityError | Status::InterruptRequest );
	}

	TryTransfer();
}

void ControllerPorts::ReloadBaudrateTimer() noexcept
{
	uint16_t factor;
	switch ( m_mode.baudrateReloadFactor )
	{
		case 2:	factor = 16;	break;
		case 3:	factor = 64;	break;
		default: factor = 1;	break;
	}
	m_baudrateTimer = ( m_baudrateReloadValue * factor ) / 2; // max value will be 21 bits
}

void ControllerPorts::TryTransfer() noexcept
{
	if ( ( m_control & Control::TXEnable ) && m_txBufferFull && ( m_state == State::Idle ) )
	{
		dbLog( "ControllerPorts::CheckTransfer()" );

		if ( m_rxBufferFull )
			dbBreakMessage( "ControllerPorts::CheckTransfer() -- RX buffer is full" );

		m_tranferringValue = m_txBuffer;
		m_txBufferFull = false;
		m_state = State::Transferring;
		m_cyclesUntilEvent = GetTransferCycles();
		m_communicateEvent->Schedule( m_cyclesUntilEvent );
	}
}

void ControllerPorts::DoTransfer()
{
	dbExpects( m_state == State::Transferring );

	// the hardware automatically enables receive when /JOYn is low
	m_control |= Control::RXEnable;

	uint8_t received = 0xff;
	bool ack = false;

	if ( stdx::any_of<uint16_t>( m_control, Control::JoyNOutput ) )
	{
		// LOW

		auto* controller = m_controllers[ stdx::any_of<uint16_t>( m_control, Control::DesiredSlotNumber ) ];
		if ( controller )
		{
			received = controller->Communicate( m_tranferringValue );
			ack = true; // TODO: when does the controller actually ack?
		}
	}
	else
	{
		// HIGH

		// TODO: memory cards?
	}

	m_rxBuffer = received;
	m_rxBufferFull = true;

	if ( ack )
	{
		m_state = State::PendingAck;
		m_cyclesUntilEvent = ControllerAckCycles; // TODO: memory card ack cycles
		m_communicateEvent->Schedule( m_cyclesUntilEvent );
	}
	else
	{
		EndTransfer();
	}
}

void ControllerPorts::DoAck()
{
	dbExpects( m_state == State::PendingAck );

	// ack is low
	m_status |= Status::AckInputLevel;

	if ( m_control & Control::ACKInterruptEnable )
	{
		m_status |= Status::InterruptRequest;
		m_interruptControl.SetInterrupt( Interrupt::ControllerAndMemoryCard );
	}

	EndTransfer();
}

void ControllerPorts::EndTransfer()
{
	// the hardware automatically clears RXEN after the transfer
	stdx::reset_bits<uint16_t>( m_control, Control::RXEnable );

	m_state = State::Idle;

	TryTransfer();
}

void ControllerPorts::UpdateCycles( uint32_t cycles )
{
	if ( m_state == State::Idle )
		return;

	dbExpects( cycles <= m_cyclesUntilEvent );

	m_cyclesUntilEvent -= cycles;
	if ( m_cyclesUntilEvent > 0 )
		return;

	switch ( m_state )
	{
		case State::Idle:
			break;

		case State::Transferring:
			DoTransfer();
			break;

		case State::PendingAck:
			DoAck();
			break;
	}
}

}