#pragma once

#include "Defs.h"

namespace PSX
{

class SerialPort
{
public:
	void Reset();

	// 32 bit registers

	uint32_t ReadData() noexcept;

	uint32_t ReadStatus() const noexcept { return m_status.value; }

	void WriteData( uint32_t value ) noexcept;

	// 16 bit registers

	uint16_t ReadMode() const noexcept
	{
		dbLogDebug( "SerialPort::Read() -- mode [%02X]", m_mode.value );
		return m_mode.value;
	}

	uint16_t ReadControl() const noexcept
	{
		dbLogDebug( "SerialPort::Read() -- control [%02X]", m_control.value );
		return m_control.value;
	}

	uint16_t ReadMisc() const noexcept
	{
		dbLogDebug( "SerialPort::Read() -- misc [%02X]", m_misc );
		return m_misc;
	}

	uint16_t ReadBaudrateReloadValue() const noexcept
	{
		dbLogDebug( "SerialPort::Read() -- baudrate reload value [%02X]", m_baudrateReloadValue );
		return m_baudrateReloadValue;
	}

	void WriteMode( uint16_t value ) noexcept
	{
		dbLogDebug( "SerialPort::Write() -- mode [%02X]", value );
		m_mode.value = value & Mode::WriteMask;
	}

	void WriteControl( uint16_t value ) noexcept;

	void WriteMisc( uint16_t value ) noexcept
	{
		dbLogWarning( "SerialPort::Write() -- misc [%02X]", value );
		m_misc = value;
	}

	void WriteBaudrateReloadValue( uint16_t value ) noexcept
	{
		dbLogDebug( "SerialPort::Write() -- baudrate reload value [%02X]", value );
		m_baudrateReloadValue = value;
		// ReloadBaudrateTimer();
	}

	void Serialize( SaveStateSerializer& serializer );

private:
	union Status
	{
		struct
		{
			uint32_t txReadyStarted : 1;		// (1=Ready/Started)  (depends on CTS) (TX requires CTS)
			uint32_t rxFifoNotEmpty : 1;		// (0=Empty, 1=Not Empty)
			uint32_t txReadyFinished : 1;		// (1=Ready/Finished) (depends on TXEN and on CTS)
			uint32_t rxParityError : 1;			// (0=No, 1=Error; Wrong Parity, when enabled) (sticky)
			uint32_t rxFifoOverrun : 1;			// (0=No, 1=Error; Received more than 8 bytes) (sticky)
			uint32_t rxBadStopBit : 1;			// (0=No, 1=Error; Bad Stop Bit) (when RXEN)   (sticky)
			uint32_t rxInputLevel : 1;			// (0=Normal, 1=Inverted) ;only AFTER receiving Stop Bit
			uint32_t dsrInputLevel : 1;			// (0=Off, 1=On) (remote DTR) ;DSR not required to be on
			uint32_t ctsInpputLevel : 1;		// (0=Off, 1=On) (remote RTS) ;CTS required for TX
			uint32_t interruptRequest : 1;		// (0=None, 1=IRQ)                             (sticky)
			uint32_t : 1;						// (always zero)
			uint32_t baudrateTimer : 15;		// (15bit timer, decrementing at 33MHz)
			uint32_t : 6;						// (usually zero, sometimes all bits set)
		};
		uint32_t value = 0;
	};
	static_assert( sizeof( Status ) == 4 );

	union Mode
	{
		struct
		{
			uint16_t baudrateReloadFactor : 2;	// (1=MUL1, 2=MUL16, 3=MUL64) (or 0=STOP)
			uint16_t characterLength : 2;		// (0=5bits, 1=6bits, 2=7bits, 3=8bits)
			uint16_t parityEnable : 1;			// (0=No, 1=Enable)
			uint16_t parityType : 1;			// (0=Even, 1=Odd) (seems to be vice-versa...?)
			uint16_t stopBitLength : 2;			// (0=Reserved/1bit, 1=1bit, 2=1.5bits, 3=2bits)
			uint16_t : 8;
		};
		uint16_t value = 0;

		static constexpr uint16_t WriteMask = 0x00ff;
	};
	static_assert( sizeof( Mode ) == 2 );


	union Control
	{
		struct
		{
			uint16_t txEnable : 1;				// (0=Disable, 1=Enable, when CTS=On)
			uint16_t dtrOutput : 1;				// (0=Off, 1=On)
			uint16_t rxEnable : 1;				// (0=Disable, 1=Enable)  ;Disable also clears RXFIFO
			uint16_t txOutputLevel : 1;			// (0=Normal, 1=Inverted, during Inactivity & Stop bits)
			uint16_t acknowledge : 1;			// (0=No change, 1=Reset SIO_STAT.Bits 3,4,5,9)      (W)
			uint16_t rtsOutputLevel : 1;		// (0=Off, 1=On)
			uint16_t reset : 1;					// (0=No change, 1=Reset most SIO_registers to zero) (W)
			uint16_t : 1;						// (read/write-able when FACTOR non-zero) (otherwise always zero)
			uint16_t rxInterruptMode : 2;		// (0..3 = IRQ when RX FIFO contains 1,2,4,8 bytes)
			uint16_t txInterruptEnable : 1;		// (0=Disable, 1=Enable) ;when SIO_STAT.0-or-2 ;Ready
			uint16_t rxInterruptEnable : 1;		// (0=Disable, 1=Enable) ;when N bytes in RX FIFO
			uint16_t dsrInterruptEnable : 1;	// (0=Disable, 1=Enable) ;when SIO_STAT.7  ;DSR=On
			uint16_t : 3;						// (always zero)
		};
		uint16_t value = 0;

		static constexpr uint16_t WriteMask = 0b11111111'11111000;
	};
	static_assert( sizeof( Control ) == 2 );

	static constexpr uint16_t DefaultBadrateReloadValue = 0x00dc;

private:
	Status m_status;
	Mode m_mode;
	Control m_control;
	uint16_t m_misc = 0;
	uint16_t m_baudrateReloadValue = 0;
};

}