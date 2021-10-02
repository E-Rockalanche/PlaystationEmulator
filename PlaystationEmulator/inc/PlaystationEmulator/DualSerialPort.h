#pragma once

#include "Defs.h"

#include <string>

namespace PSX
{

class DualSerialPort
{
public:
	struct Register
	{
		// read/write pairs occupy the same address
		enum : uint32_t
		{
			ModeA = 0,

			StatusA = 1,
			ClockSelectA = 1,

			ToggleBaudRateGeneratorTestMode = 2,
			CommandA = 2,

			RXHoldingA = 3,
			TXHoldingA = 3,

			InputPortChange = 4,
			AuxControl = 4,

			InterruptStatus = 5,
			InterruptMask = 5,

			TimerCurrentValueUpper = 6,
			TimerReloadValueUpper = 6,

			TimerCurrentValueLower = 7,
			TimerReloadValueLower = 7,

			ModeB = 8,

			StatusB = 9,
			ClockSelectB = 9,

			Toggle1X16XTestMode = 10,
			CommandB = 10,

			RXHoldingB = 11,
			TXHoldingB = 11,

			Reserved = 12,

			InputPort = 13,
			OutputPortConfiguration = 13,

			StartCounterCommand = 14,
			SetOutputPortBits = 14,

			StopCounterCommand = 15,
			ResetOutputPportBits = 15,
		};
	};

	void Write( uint32_t offset, uint8_t value ) noexcept
	{
		switch ( offset )
		{
			case Register::TXHoldingA:
			case Register::TXHoldingB:
				m_log += char( value );
				if ( char( value ) == '\n' )
					dbBreakMessage( "\n########## LOG UPDATE ##########%s\n", m_log.c_str() );
				break;
			default:
				break;
		}
	}

	uint8_t Read( uint32_t offset ) const noexcept
	{
		switch ( offset )
		{
			case Register::StatusA:
			case Register::StatusB:
				return 0b00000100;

			default:
				return 0;
		}
	}

private:
	std::string m_log;
};

}