#pragma once

#include "Defs.h"

#include <array>

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

	union Status
	{
		struct
		{
			uint32_t txReadyStarted : 1;
			uint32_t rxFifoNotEmpty : 1;
			uint32_t txReadyFinished : 1;
			uint32_t rxParityError : 1;
			uint32_t : 3;
			uint32_t ackInputLow : 1;
			uint32_t : 1;
			uint32_t interruptRequest : 1;
			uint32_t : 1;
			uint32_t baudrateTimer : 21;
		};
		uint32_t value = 0;
	};
	static_assert( sizeof( Status ) == 4 );

	union Mode
	{
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
		uint16_t value = 0;

		static constexpr uint16_t WriteMask = 0b0000000100111111;

		uint16_t GetCharacterLength() const noexcept { return characterLength + 5; }
	};
	static_assert( sizeof( Mode ) == 2 );

	union Control
	{
		struct
		{
			uint16_t txEnable : 1;
			uint16_t selectLow : 1;
			uint16_t rxEnable : 1;
			uint16_t : 1;
			uint16_t acknowledge : 1;
			uint16_t : 1;
			uint16_t reset : 1;
			uint16_t : 1;

			uint16_t rxInterruptMode : 2;
			uint16_t txInterruptEnable : 1;
			uint16_t rxInterruptEnable : 1;
			uint16_t ackInterruptEnable : 1;
			uint16_t desiredSlotNumber : 1;
			uint16_t : 2;
		};
		uint16_t value = 0;

		static constexpr uint16_t WriteMask = 0b00111111'01111111;
	};
	static_assert( sizeof( Control ) == 2 );

	ControllerPorts( InterruptControl& interruptControl, EventManager& eventManager );
	~ControllerPorts();

	void Reset();

	// 32bit registers

	uint32_t ReadData() noexcept;

	uint32_t ReadStatus() const noexcept { return m_status.value; }

	void WriteData( uint32_t value ) noexcept;

	// 16bit registers

	uint16_t ReadMode() const noexcept
	{
		dbLogDebug( "ControllerPorts::Read() -- mode [%X]", m_mode.value );
		return m_mode.value;
	}

	uint16_t ReadControl() const noexcept
	{
		dbLogDebug( "ControllerPorts::Read() -- control [%X]", m_control );
		return m_control.value;
	}

	uint16_t ReadBaudrateReloadValue() const noexcept
	{
		dbLogDebug( "ControllerPorts::Read() -- baudrate reload value [%X]", m_baudrateReloadValue );
		return m_baudrateReloadValue;
	}

	void WriteMode( uint16_t value ) noexcept
	{
		dbLogDebug( "ControllerPorts::Write() -- mode [%X]", value );
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
		dbLogDebug( "ControllerPorts::Write() -- baudrate reload value [%X]", value );
		m_baudrateReloadValue = static_cast<uint16_t>( value );
		ReloadBaudrateTimer();
	}

	void SetController( size_t slot, Controller* controller )
	{
		m_controllers[ slot ] = controller;
	}

	void SetMemoryCard( size_t slot, MemoryCard* memCard )
	{
		m_memCards[ slot ] = memCard;
	}

	void Serialize( SaveStateSerializer& serializer );

private:
	enum class State
	{
		Idle,
		Transferring,
		AckPending,
		AckLow
	};

	enum class CurrentDevice
	{
		None,
		Controller,
		MemoryCard,
	};

	static constexpr uint32_t ControllerAckCycles = 338;
	static constexpr uint32_t MemoryCardAckCycles = 170;
	
	static constexpr uint32_t AckLowCycles = 100; // nocash docs

private:
	void UpdateStatus() noexcept;

	void ReloadBaudrateTimer() noexcept;

	void TryTransfer() noexcept;

	uint32_t GetTransferCycles() const noexcept
	{
		return m_baudrateReloadValue * 8; // ignore reload factor and character length
	}

	void DoTransfer();
	void DoAck();
	void EndTransfer();

	void UpdateCommunication();

	void SerializeController( SaveStateSerializer& serializer, size_t slot );
	void SerializeMemoryCard( SaveStateSerializer& serializer, size_t slot );

private:
	InterruptControl& m_interruptControl;
	EventHandle m_communicateEvent;

	// registers
	Status m_status;
	Mode m_mode;
	Control m_control;
	uint16_t m_baudrateReloadValue = 0;

	State m_state = State::Idle;
	CurrentDevice m_currentDevice = CurrentDevice::None;

	uint8_t m_txBuffer = 0;
	bool m_txBufferFull = false;

	uint8_t m_rxBuffer = 0;
	bool m_rxBufferFull = false;

	uint8_t m_tranferringValue = 0;

	std::array<Controller*, 2> m_controllers{};
	std::array<MemoryCard*, 2> m_memCards{};
};

}