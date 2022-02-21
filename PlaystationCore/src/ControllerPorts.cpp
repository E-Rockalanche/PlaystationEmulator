#include "ControllerPorts.h"

#include "Controller.h"
#include "EventManager.h"
#include "InterruptControl.h"
#include "MemoryCard.h"
#include "SaveState.h"

namespace PSX
{

ControllerPorts::ControllerPorts( InterruptControl& interruptControl, EventManager& eventManager )
	: m_interruptControl{ interruptControl }
{
	m_communicateEvent = eventManager.CreateEvent( "ControllerPorts communicate event", [this]( cycles_t ) { UpdateCommunication(); } );
}

ControllerPorts::~ControllerPorts() = default;

void ControllerPorts::Reset()
{
	m_communicateEvent->Reset();

	m_status.value = 0;
	m_mode.value = 0;
	m_control.value = 0;
	m_baudrateReloadValue = 0x0088;

	m_state = State::Idle;
	m_currentDevice = CurrentDevice::None;

	m_txBuffer = 0;
	m_txBufferFull = false;

	m_rxBuffer = 0;
	m_rxBufferFull = false;

	m_tranferringValue = 0;

	for ( size_t i = 0; i < 2; ++i )
	{
		if ( m_controllers[ i ] )
			m_controllers[ i ]->Reset();

		if ( m_memCards[ i ] )
			m_memCards[ i ]->Reset();
	}

	UpdateStatus();
}

uint32_t ControllerPorts::ReadData() noexcept
{
	// A data byte can be read when JOY_STAT.1=1. Data should be read only via 8bit memory access
	// (the 16bit/32bit "preview" feature is rather unusable, and usually there shouldn't be more than 1 byte in the FIFO anyways).

	uint8_t data = 0xff;
	if ( m_rxBufferFull )
	{
		data = m_rxBuffer;
		m_rxBufferFull = false;
		UpdateStatus();
	}

	dbLogDebug( "ControllerPorts::Read() -- data [%X]", data );
	return data | ( data << 8 ) | ( data << 16 ) | ( data << 24 );
}

void ControllerPorts::WriteData( uint32_t value ) noexcept
{
	// Writing to this register starts the transfer (if, or as soon as TXEN=1 and JOY_STAT.2=Ready),
	// the written value is sent to the controller or memory card, and, simultaneously,
	// a byte is received (and stored in RX FIFO if JOY_CTRL.1 or JOY_CTRL.2 is set).
	dbLogDebug( "ControllerPorts::Write() -- data [%X]", value );

	if ( m_txBufferFull )
		dbLogWarning( "ControllerPorts::WriteData() -- TX buffer is full" );

	m_txBuffer = static_cast<uint8_t>( value );
	m_txBufferFull = true;

	TryTransfer();
}

void ControllerPorts::WriteControl( uint16_t value ) noexcept
{
	dbLogDebug( "ControllerPorts::Write() -- control [%X]", value );
	m_control.value = value & Control::WriteMask;

	if ( m_control.reset )
	{
		// soft reset
		m_control.value = 0;
		m_status.value = 0;
		m_mode.value = 0;

		m_txBuffer = 0;
		m_txBufferFull = false;
		m_rxBuffer = 0;
		m_rxBufferFull = false;

		m_state = State::Idle;
		m_communicateEvent->Cancel();
	}

	if ( m_control.acknowledge )
	{
		// acknowledge interrupt
		// TODO: IRQ is not edge triggered. Must wait until ack is high
		m_status.rxParityError = false;
		m_status.interruptRequest = false;
	}

	if ( !m_control.selectLow )
	{
		m_currentDevice = CurrentDevice::None;

		for ( size_t i = 0; i < 2; ++i )
		{
			if ( m_controllers[ i ] )
				m_controllers[ i ]->ResetTransfer();

			if ( m_memCards[ i ] )
				m_memCards[ i ]->ResetTransfer();
		}
	}

	if ( m_control.selectLow && m_control.txEnable )
	{
		TryTransfer();
	}
	else
	{
		m_state = State::Idle;
		m_communicateEvent->Cancel();
	}

	UpdateStatus();
}

void ControllerPorts::UpdateStatus() noexcept
{
	m_status.rxFifoNotEmpty = m_rxBufferFull;
	m_status.txReadyStarted = !m_txBufferFull;
	m_status.txReadyFinished = m_txBufferFull && ( m_state != State::Transferring );
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
	m_status.baudrateTimer = ( m_baudrateReloadValue * factor ) / 2; // max value will be 21 bits
}

void ControllerPorts::TryTransfer() noexcept
{
	if ( m_txBufferFull && m_control.selectLow && m_control.txEnable && ( m_state == State::Idle ) )
	{
		dbLogDebug( "ControllerPorts::TryTransfer -- transferring" );
		m_tranferringValue = m_txBuffer;
		m_txBufferFull = false;
		m_control.rxEnable = true;
		m_state = State::Transferring;
		m_communicateEvent->Schedule( GetTransferCycles() );
	}

	UpdateStatus();
}

void ControllerPorts::DoTransfer()
{
	dbExpects( m_state == State::Transferring );

	uint8_t output = 0xff;
	bool acked = false;

	Controller* controller = m_controllers[ m_control.desiredSlotNumber ];
	MemoryCard* memCard = m_memCards[ m_control.desiredSlotNumber ];

	switch ( m_currentDevice )
	{
		case CurrentDevice::None:
		{
			if ( controller && controller->Communicate( m_tranferringValue, output ) )
			{
				m_currentDevice = CurrentDevice::Controller;
				acked = true;
			}
			else if ( memCard && memCard->Communicate( m_tranferringValue, output ) )
			{
				m_currentDevice = CurrentDevice::MemoryCard;
				acked = true;
			}
			break;
		}

		case CurrentDevice::Controller:
		{
			if ( controller && controller->Communicate( m_tranferringValue, output ) )
				acked = true;
			else
				m_currentDevice = CurrentDevice::None;

			break;
		}

		case CurrentDevice::MemoryCard:
		{
			if ( memCard && memCard->Communicate( m_tranferringValue, output ) )
				acked = true;
			else
				m_currentDevice = CurrentDevice::None;
		}
	}

	m_rxBuffer = output;
	m_rxBufferFull = true;

	if ( acked )
	{
		m_state = State::AckPending;
		const cycles_t ackCycles = ( m_currentDevice == CurrentDevice::Controller ) ? ControllerAckCycles : MemoryCardAckCycles;
		m_communicateEvent->Schedule( ackCycles );
	}
	else
	{
		EndTransfer();
	}

	UpdateStatus();
}

void ControllerPorts::DoAck()
{
	dbExpects( m_state == State::AckPending );

	m_status.ackInputLow = true;

	if ( m_control.ackInterruptEnable )
	{
		m_status.interruptRequest = true;
		m_interruptControl.SetInterrupt( Interrupt::ControllerAndMemoryCard );
	}

	m_state = State::AckLow;
	m_communicateEvent->Schedule( AckLowCycles );

	UpdateStatus();
}

void ControllerPorts::EndTransfer()
{
	m_status.ackInputLow = false;
	m_state = State::Idle;
	TryTransfer();
}

void ControllerPorts::UpdateCommunication()
{
	switch ( m_state )
	{
		case State::Idle:
			dbBreak();
			break;

		case State::Transferring:
			DoTransfer();
			break;

		case State::AckPending:
			DoAck();
			break;

		case State::AckLow:
			EndTransfer();
			break;
	}
}


ControllerType ControllerPorts::GetControllerType( size_t slot ) const
{
	return m_controllers[ slot ] ? m_controllers[ slot ]->GetType() : ControllerType::None;
}

void ControllerPorts::Serialize( SaveStateSerializer& serializer )
{
	if ( !serializer.Header( "ControllerPorts", 1 ) )
		return;

	m_communicateEvent->Serialize( serializer );

	serializer( m_status.value );
	serializer( m_mode.value );
	serializer( m_control.value );
	serializer( m_baudrateReloadValue );

	serializer( m_state );
	serializer( m_currentDevice );

	serializer( m_txBuffer );
	serializer( m_txBufferFull );

	serializer( m_rxBuffer );
	serializer( m_rxBufferFull );

	serializer( m_tranferringValue );

	SerializeController( serializer, 0 );
	SerializeController( serializer, 1 );

	SerializeMemoryCard( serializer, 0 );
	SerializeMemoryCard( serializer, 1 );
}

void ControllerPorts::SerializeController( SaveStateSerializer& serializer, size_t slot )
{
	ControllerType type = GetControllerType( slot );
	serializer( type );
	if ( type != GetControllerType( slot ) )
	{
		// TODO: create or ignore controller
		dbAssert( serializer.Reading() );
		serializer.SetError();
		return;
	}

	if ( type != ControllerType::None )
		m_controllers[ slot ]->Serialize( serializer );
}

void ControllerPorts::SerializeMemoryCard( SaveStateSerializer& serializer, size_t slot )
{
	bool hasMemCard = HasMemoryCard( slot );
	serializer( hasMemCard );
	if ( hasMemCard != HasMemoryCard( slot ) )
	{
		// TODO: create or ignore memory card
		dbAssert( serializer.Reading() );
		serializer.SetError();
		return;
	}

	if ( hasMemCard )
		m_memCards[ slot ]->Serialize( serializer );
}

}