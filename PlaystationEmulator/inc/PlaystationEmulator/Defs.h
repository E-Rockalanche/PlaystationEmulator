#pragma once

#include <stdx/assert.h>

#include <cstdint>
#include <limits>
#include <memory>

struct SDL_Window;

namespace PSX
{

using cycles_t = int;

constexpr cycles_t InfiniteCycles = std::numeric_limits<cycles_t>::max();

constexpr cycles_t CpuCyclesPerSecond = 44100 * 0x300; // 33868800

class Event;
class EventManager;
class CDRom;
class CDRomDrive;
class Controller;
class ControllerPorts;
class Dma;
class DualSerialPort;
class Event;
class EventManager;
class Gpu;
class InterruptControl;
class MacroblockDecoder;
class MemoryCard;
class MemoryControl;
class MemoryMap;
class MipsR3000Cpu;
class Playstation;
class Renderer;
class Spu;
class Timers;

using EventHandle = std::unique_ptr<Event>;

template <size_t N, typename To, typename From>
inline constexpr To SignExtend( From from ) noexcept
{
	static_assert( N <= sizeof( From ) * 8 );
	static_assert( N < sizeof( To ) * 8 );

	constexpr To Extension = To( -1 ) << N;
	constexpr From Mask = static_cast<From>( ~Extension );
	constexpr From SignBit = 1 << ( N - 1 );

	return static_cast<To>( ( from & Mask ) | ( ( from & SignBit ) ? Extension : 0 ) );
}

}