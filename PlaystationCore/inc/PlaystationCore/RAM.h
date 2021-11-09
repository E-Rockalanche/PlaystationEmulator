#pragma once

#include "Memory.h"

namespace PSX
{

static constexpr uint32_t RamSize = 2 * 1024 * 1024;
static constexpr uint32_t RamAddressMask = RamSize - 1;

using Ram = Memory<RamSize>;

static constexpr uint32_t ScratchpadSize = 1024;
static constexpr uint32_t ScratchpadAddressMask = ScratchpadSize - 1;

using Scratchpad = Memory<ScratchpadSize>;

}