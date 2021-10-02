#pragma once

#include <stdx/assert.h>

#include <cstdint>
#include <limits>
#include <memory>

namespace PSX
{

using cycles_t = int;

constexpr cycles_t InfiniteCycles = std::numeric_limits<cycles_t>::max();

class Event;
class EventManager;
class CDRom;
class CDRomDrive;
class Controller;
class ControllerPorts;
class Cpu;
class Dma;
class Gpu;
class Gte;
class InterruptControl;
class MemoryControl;
class Renderer;
class Spu;
class Timers;

using EventHandle = std::unique_ptr<Event>;

}