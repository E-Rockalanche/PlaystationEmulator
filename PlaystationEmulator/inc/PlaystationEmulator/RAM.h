#pragma once

#include "Memory.h"

namespace PSX
{

using Ram = Memory<2 * 1024 * 1024>;
using Scratchpad = Memory<1024>;

}