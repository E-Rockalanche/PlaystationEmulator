#pragma once

#include "Memory.h"

namespace PSX
{

constexpr uint32_t BiosSize = 512 * 1024;

using Bios = Memory<BiosSize>;

bool LoadBios( const char* filename, Bios& bios );

}