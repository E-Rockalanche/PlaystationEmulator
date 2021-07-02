#pragma once

#include "FifoBuffer.h"

#include <cstdint>

namespace PSX
{

class PeripheralPorts
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
			BaudrateTimerMask = 0x1fffffu << 11,
		};
	};

	union Mode
	{
		static constexpr uint16_t WriteMask = 0x013f;

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

	union Control
	{
		static constexpr uint16_t WriteMask = 0x3f7f;

		struct
		{
			uint16_t txEnable : 1;
			uint16_t joynOutput : 1;
			uint16_t rxEnable : 1;
			uint16_t : 1; // unknown
			uint16_t acknowledge : 1;
			uint16_t : 1; // unkown
			uint16_t reset : 1;
			uint16_t : 1; // always 0
			uint16_t rxInterruptMode : 2;
			uint16_t txInterruptEnable : 1;
			uint16_t rxInterruptEnable : 1;
			uint16_t ackInterruptEnable : 1;
			uint16_t desiredSlotNumber : 1;
			uint16_t : 2; // always 0;
		};
		uint16_t value;
	};
	static_assert( sizeof( Control ) == 2 );

	void Reset()
	{
		m_status = 0;
		m_baudrateTimer = 0;

		m_mode.value = 0;
		m_mode.baudrateReloadFactor = 1;
		// TODO: mode defaults to 8bit and no parity?

		m_control.value = 0;
		m_baudrateReloadValue = 0x0088;

		m_txBuffer.Reset();
		m_rxBuffer.Reset();
	}

	uint32_t Read( uint32_t index )
	{
		dbExpects( index < 5 );
		switch ( static_cast<Register>( index ) )
		{
			case Register::Data:
				// A data byte can be read when JOY_STAT.1=1. Data should be read only via 8bit memory access
				// (the 16bit/32bit "preview" feature is rather unusable, and usually there shouldn't be more than 1 byte in the FIFO anyways).
				dbLog( "PeripheralPorts::Read() -- Data" );
				return *reinterpret_cast<const uint32_t*>( m_rxBuffer.Data() );

			case Register::Status:
				dbLog( "PeripheralPorts::Read() -- Status" );
				return m_status | ( m_baudrateTimer << 11 ) | ( !m_rxBuffer.Empty() << 1 );

			case Register::Mode:
				dbLog( "PeripheralPorts::Read() -- Mode" );
				return m_mode.value;

			case Register::Control:
				dbLog( "PeripheralPorts::Read() -- Control" );
				return m_control.value;

			case Register::Baudrate:
				dbLog( "PeripheralPorts::Read() -- BaudrateReloadValue" );
				return m_baudrateReloadValue;

			default:
				dbBreak();
				return 0;
		}
	}

	void Write( uint32_t index, uint32_t value )
	{
		dbExpects( index < 5 );
		switch ( static_cast<Register>( index ) )
		{
			case Register::Data:
				// Writing to this register starts the transfer (if, or as soon as TXEN=1 and JOY_STAT.2=Ready),
				// the written value is sent to the controller or memory card, and, simultaneously,
				// a byte is received (and stored in RX FIFO if JOY_CTRL.1 or JOY_CTRL.2 is set).
				dbLog( "PeripheralPorts::Write() -- Data [%X]", value );
				// m_txBuffer.Push( static_cast<uint8_t>( value ) );
				// TODO: start transfer to controller/memory card
				break;

			case Register::Status:
				dbBreakMessage( "PeripheralPorts::Write() -- Cannot write to status" );
				break;

			case Register::Mode:
				dbLog( "PeripheralPorts::Write() -- Mode [%X]", value );
				m_mode.value = static_cast<uint16_t>( value ) & Mode::WriteMask;
				break;

			case Register::Control:
				dbLog( "PeripheralPorts::Write() -- Control [%X]", value );
				m_control.value = static_cast<uint16_t>( value ) & Control::WriteMask;
				break;

			case Register::Baudrate:
				// Timer reload occurs when writing to this register, and, automatically when the Baudrate Timer reaches zero.Upon reload,
				// the 16bit Reload value is multiplied by the Baudrate Factor( see 1F801048h.Bit0 - 1 ),
				// divided by 2, and then copied to the 21bit Baudrate Timer( 1F801044h.Bit11 - 31 ).
				// The 21bit timer decreases at 33MHz, and, it ellapses twice per bit( once for CLK = LOW and once for CLK = HIGH ).
				// BitsPerSecond = ( 44100Hz * 300h ) / MIN( ( ( Reload*Factor ) AND NOT 1 ), 1 )
				// The default BAUD value is 0088h( equivalent to 44h cpu cycles ), and default factor is MUL1, so CLK pulses are 44h cpu cycles LOW,
				// and 44h cpu cycles HIGH, giving it a transfer rate of circa 250kHz per bit( 33MHz divided by 88h cycles ).
				// Note: The Baudrate Timer is always running; even if there's no transfer in progress.
				dbLog( "PeripheralPorts::Write() -- BaudrateReloadValue [%X]", value );
				m_baudrateReloadValue = static_cast<uint16_t>( value );
				ReloadBaudrateTimer();
				break;

			default:
				dbBreak();
				break;
		}
	}

private:
	void ReloadBaudrateTimer()
	{
		uint16_t factor = 1;
		switch ( m_mode.baudrateReloadFactor )
		{
			case 2:	factor = 16;	break;
			case 3:	factor = 64;	break;
		}
		m_baudrateTimer = ( m_baudrateReloadValue * factor ) / 2; // max value will be 21 bits
	}

private:
	uint32_t m_status;
	uint32_t m_baudrateTimer;
	Mode m_mode;
	Control m_control;
	uint16_t m_baudrateReloadValue;

	FifoBuffer<uint8_t, 2> m_txBuffer;
	FifoBuffer<uint8_t, 8> m_rxBuffer;
};

}