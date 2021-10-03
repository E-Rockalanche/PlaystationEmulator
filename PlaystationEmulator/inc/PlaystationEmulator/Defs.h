#pragma once

#include <stdx/assert.h>

#include <cstdint>
#include <limits>
#include <memory>

struct SDL_Window;

namespace PSX
{

using cycles_t = uint32_t;

constexpr cycles_t InfiniteCycles = std::numeric_limits<cycles_t>::max();

class Event;
class EventManager;
class CDRom;
class CDRomDrive;
class Controller;
class ControllerPorts;
class CycleScheduler;
class Dma;
class DualSerialPort;
class Gpu;
class InterruptControl;
class MemoryControl;
class MemoryMap;
class MipsR3000Cpu;
class Playstation;
class Renderer;
class Spu;
class Timers;

using EventHandle = std::unique_ptr<Event>;

}