#pragma once

#include "Defs.h"

#include "FifoBuffer.h"

#include <stdx/bit.h>

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
			AckInputLevel = 1 << 7, // 0=high, 1=low
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
			TXInterruptEnable = 1 << 10,
			RXInterruptEnable = 1 << 11,
			ACKInterruptEnable = 1 << 12,
			DesiredSlotNumber = 1 << 13,

			WriteMask = 0b0011111101111111
		};
	};

	ControllerPorts( InterruptControl& interruptControl, EventManager& eventManager );

	void Reset();

	// 32bit registers

	uint32_t ReadData() noexcept;

	uint32_t ReadStatus() noexcept;

	void WriteData( uint32_t value ) noexcept;

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

	void WriteControl( uint16_t value ) noexcept;

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

	void SetController( size_t slot, Controller* controller )
	{
		m_controllers[ slot ] = controller;
	}

private:
	enum class State
	{
		Idle,
		Transferring,
		PendingAck
	};

	// values copied from duckstation
	static constexpr uint32_t ControllerAckCycles = 450;
	static constexpr uint32_t MemoryCardAckCycles = 170;

	void ReloadBaudrateTimer() noexcept;

	uint16_t GetRXInterruptMode() const noexcept
	{
		return ( m_control << 8 ) & 0x3;
	}

	bool IsFinishedTransfer() const noexcept
	{
		return !m_txBufferFull && ( m_state == State::Idle );
	}

	bool IsTransferring() const noexcept
	{
		return m_state != State::Idle;
	}

	void TryTransfer() noexcept;

	uint32_t GetTransferCycles() const noexcept
	{
		return m_baudrateReloadValue * 8; // baudrate * 8 bits/byte
	}

	void DoTransfer();
	void DoAck();
	void EndTransfer();

	void UpdateCycles( uint32_t cycles );

	// TEMP until the cycles shceduler is improved
	void UpdateCyclesBeforeRead() const noexcept;

private:
	InterruptControl& m_interruptControl;
	Event* m_communicateEvent = nullptr;

	uint32_t m_status = 0;
	uint32_t m_baudrateTimer = 0;
	Mode m_mode{};
	uint16_t m_control = 0;
	uint16_t m_baudrateReloadValue = 0;

	State m_state = State::Idle;
	uint32_t m_cyclesUntilEvent = 0;

	uint8_t m_txBuffer = 0;
	uint8_t m_tranferringValue = 0;
	bool m_txBufferFull = false;

	uint8_t m_rxBuffer = 0;
	bool m_rxBufferFull = false;

	std::array<Controller*, 2> m_controllers{};
};

}