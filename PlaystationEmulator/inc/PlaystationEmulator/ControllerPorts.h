#pragma once

#include "FifoBuffer.h"

#include <stdx/bit.h>

#include <cstdint>

namespace PSX
{

class ControllerPorts
{
public:

	enum class Register
	{
		Data,
		Status,
		Mode,
		Control,
		Baudrate
	};

	struct Status
	{
		enum : uint32_t
		{
			TxReadyFlag1 = 1 << 0,
			RxFifoNotEmpty = 1 << 1, // tied to rx buffer
			TxReadyFlag2 = 1 << 2,
			RxParityError = 1 << 3,
			AckInputLevel = 1 << 7,
			InterruptRequest = 1 << 9,
			BaudrateTimerMask = 0x1fffffu << 11
		};
	};

	union Mode
	{
		static constexpr uint16_t WriteMask = 0b0000000100111111;

		Mode() : value{ 0 } {}

		struct
		{
			uint16_t baudrateReloadFactor : 2;
			uint16_t characterLength : 2;
			uint16_t parityEnable : 1;
			uint16_t parityType : 1;
			uint16_t : 2; // always 0
			uint16_t clockOutputPolarity : 1;
			uint16_t : 7; // always 0
		};
		uint16_t value;
	};
	static_assert( sizeof( Mode ) == 2 );

	// length in bits
	enum class CharacterLength
	{
		Five,
		Six,
		Seven,
		Eight
	};

	struct Control
	{
		enum : uint16_t
		{
			TXEnable = 1 << 0,
			JoyNOutput = 1 << 1,
			RXEnable = 1 << 2,
			Acknowledge = 1 << 4,
			Reset = 1 << 6,
			RXInterruptMode = 0x3 << 8,
			TXInterruptMode = 1 << 10,
			RXInterruptEnable = 1 << 11,
			ACKInterruptEnable = 1 << 12,
			DesiredSlotNumber = 1 << 13,

			WriteMask = 0b0011111101111111
		};
	};

	void Reset()
	{
		m_status = 0;
		m_baudrateTimer = 0;

		m_mode.value = 0;
		m_mode.baudrateReloadFactor = 1;
		// TODO: mode defaults to 8bit and no parity?

		m_control = 0;
		m_baudrateReloadValue = 0x0088;

		m_txBuffer = 0;
		m_txBufferFull = false;

		m_rxBuffer = 0;
		m_rxBufferFull = false;
	}

	// 32bit registers

	uint32_t ReadData() noexcept
	{
		// A data byte can be read when JOY_STAT.1=1. Data should be read only via 8bit memory access
		// (the 16bit/32bit "preview" feature is rather unusable, and usually there shouldn't be more than 1 byte in the FIFO anyways).

		const uint8_t data = m_rxBufferFull ? m_rxBuffer : 0xff;
		m_rxBufferFull = false;

		dbLog( "ControllerPorts::Read() -- data [%X]", data );
		return data | ( data << 8 ) | ( data << 16 ) | ( data << 24 );
	}

	uint32_t ReadStatus() const noexcept
	{
		const uint32_t status = m_status |
			( m_baudrateTimer << 11 ) |
			( m_rxBufferFull << 1 ) |
			( (uint32_t)!m_txBufferFull ) |
			( ( !m_txBufferFull && !m_transfering ) << 2 );

		dbLog( "ControllerPorts::Read() -- status [%X]", status );
		return status;
	}

	void WriteData( uint32_t value ) noexcept
	{
		// Writing to this register starts the transfer (if, or as soon as TXEN=1 and JOY_STAT.2=Ready),
		// the written value is sent to the controller or memory card, and, simultaneously,
		// a byte is received (and stored in RX FIFO if JOY_CTRL.1 or JOY_CTRL.2 is set).
		dbLog( "ControllerPorts::Write() -- data [%X]", value );

		if ( m_txBufferFull )
			dbLogWarning( "ControllerPorts::WriteData() -- TX buffer is full" );

		m_txBuffer = static_cast<uint8_t>( value );
		m_txBufferFull = true;

		CheckTransfer();
	}

	// 16bit registers

	uint16_t ReadMode() const noexcept
	{
		dbLog( "ControllerPorts::Read() -- mode [%X]", m_mode.value );
		return m_mode.value;
	}

	uint16_t ReadControl() const noexcept
	{
		dbLog( "ControllerPorts::Read() -- control [%X]", m_control );
		return m_control;
	}

	uint16_t ReadBaudrateReloadValue() const noexcept
	{
		dbLog( "ControllerPorts::Read() -- baudrate reload value [%X]", m_baudrateReloadValue );
		return m_baudrateReloadValue;
	}

	void WriteMode( uint16_t value ) noexcept
	{
		dbLog( "ControllerPorts::Write() -- mode [%X]", value );
		m_mode.value = static_cast<uint16_t>( value ) & Mode::WriteMask;
	}

	void WriteControl( uint16_t value ) noexcept
	{
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

		CheckTransfer();
	}

	void WriteBaudrateReloadValue( uint16_t value ) noexcept
	{
		// Timer reload occurs when writing to this register, and, automatically when the Baudrate Timer reaches zero.Upon reload,
		// the 16bit Reload value is multiplied by the Baudrate Factor( see 1F801048h.Bit0 - 1 ),
		// divided by 2, and then copied to the 21bit Baudrate Timer( 1F801044h.Bit11 - 31 ).
		// The 21bit timer decreases at 33MHz, and, it ellapses twice per bit( once for CLK = LOW and once for CLK = HIGH ).
		// BitsPerSecond = ( 44100Hz * 300h ) / MIN( ( ( Reload*Factor ) AND NOT 1 ), 1 )
		// The default BAUD value is 0088h( equivalent to 44h cpu cycles ), and default factor is MUL1, so CLK pulses are 44h cpu cycles LOW,
		// and 44h cpu cycles HIGH, giving it a transfer rate of circa 250kHz per bit( 33MHz divided by 88h cycles ).
		// Note: The Baudrate Timer is always running; even if there's no transfer in progress.
		dbLog( "ControllerPorts::Write() -- baudrate reload value [%X]", value );
		m_baudrateReloadValue = static_cast<uint16_t>( value );
		ReloadBaudrateTimer();
	}

private:
	void ReloadBaudrateTimer() noexcept
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

	uint16_t GetRXInterruptMode() const noexcept
	{
		return ( m_control << 8 ) & 0x3;
	}

	void CheckTransfer()
	{
		if ( ( m_control & Control::TXEnable ) && m_txBufferFull && !m_transfering )
		{
			dbLog( "ControllerPorts::CheckTransfer()" );

			if ( m_rxBufferFull )
				dbLogWarning( "ControllerPorts::CheckTransfer() -- RX buffer is full" );

			m_rxBuffer = 0xff; // nothing plugged in
			m_rxBufferFull = true;

			m_txBufferFull = false;

			m_transfering = false; // TODO: timing
		}
	}

private:
	uint32_t m_status = 0;
	uint32_t m_baudrateTimer = 0;
	Mode m_mode{};
	uint16_t m_control = 0;
	uint16_t m_baudrateReloadValue = 0;

	uint8_t m_txBuffer = 0;
	bool m_txBufferFull = false;

	uint8_t m_rxBuffer = 0;
	bool m_rxBufferFull = false;

	bool m_transfering = false;
};

}