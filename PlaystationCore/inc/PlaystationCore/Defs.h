#pragma once

#include <stdx/assert.h>

#include <cstdint>
#include <limits>
#include <memory>

#ifndef STDX_SHIPPING
#define PSX_HOOK_EXE
#endif

#ifndef STDX_SHIPPING
#define PSX_HOOK_BIOS
#endif

struct SDL_Window;

namespace PSX
{

using cycles_t = int32_t;

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
class SaveStateSerializer;
class SerialPort;
class Spu;
class Timers;
class AudioQueue;

template <size_t> class Memory;
using Bios = Memory<512 * 1024>;
using Ram = Memory<2 * 1024 * 1024>;
using Scratchpad = Memory<1024>;

struct Instruction;

using EventHandle = std::unique_ptr<Event>;

enum class ControllerType
{
	None,
	Digital,
	Analog
};

template <size_t N, typename To, typename From>
inline constexpr To SignExtend( From value ) noexcept
{
	static_assert( N <= sizeof( To ) * 8 );
	static_assert( N <= sizeof( From ) * 8 );

	constexpr size_t Shift = sizeof( To ) * 8 - N;
	using SignedTo = std::make_signed_t<To>;

	SignedTo s = static_cast<SignedTo>( value ) << Shift;
	s >>= Shift;
	return static_cast<To>( s );
}

}