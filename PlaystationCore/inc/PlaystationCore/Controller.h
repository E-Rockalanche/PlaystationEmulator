#pragma once

#include "Defs.h"

#include <stdx/bit.h>

namespace PSX
{

/*
0-3 number of following halfwords
4-7 controller type
8-15 fixed (5A)
*/
enum class ControllerID : uint16_t
{
	Mouse = 0x5a12, // two button mouse
	NegCon = 0x5a23, // steering twist/wheel/paddle
	KonamiLightgun = 0x5a31, // IRQ10-type
	DigitalPad = 0x5a41, // or analog pad in digital mode (LED=off)
	AnalogStick = 0x5a53, // or analog pad in fight mode (LED=green)
	NamcoLightgun = 0x5a63, // Cinch-type
	AnalogPad = 0x5a73, // normal analog mode (LED=red)
	Multitap = 0x5a80, // multiplayer adaptor
	Jogcon = 0x5ae3, // steering dial
	ConfigMode = 0x5af3, // configuration mode, see rumble command 0x43
	HighZ = 0xffff, // no controller connected
};

enum class Button: uint16_t
{
	Select = 1,
	L3 = 1 << 1,
	R3 = 1 << 2,
	Start = 1 << 3,
	Up = 1 << 4,
	Right = 1 << 5,
	Down = 1 << 6,
	Left = 1 << 7,
	L2 = 1 << 8,
	R2 = 1 << 9,
	L1 = 1 << 10,
	R1 = 1 << 11,
	Triangle = 1 << 12,
	Circle = 1 << 13,
	X = 1 << 14,
	Square = 1 << 15
};

class Controller
{
public:
	static constexpr ControllerID ID = ControllerID::DigitalPad;

	bool Communicate( uint8_t input, uint8_t& output );

	void Press( Button button )
	{
		stdx::reset_bits( m_buttons, static_cast<uint16_t>( button ) );
	}

	void Release( Button button )
	{
		stdx::set_bits( m_buttons, static_cast<uint16_t>( button ) );
	}

	void Reset()
	{
		ResetTransfer();
	}

	void ResetTransfer()
	{
		m_state = State::Idle;
	}

private:
	enum class State
	{
		Idle,
		IdLow,
		IdHigh,
		ButtonsLow,
		ButtonsHigh
	};

	static constexpr uint8_t HighZ = 0xff;

private:

	State m_state = State::Idle;
	uint16_t m_buttons = 0xffffu;
};

}