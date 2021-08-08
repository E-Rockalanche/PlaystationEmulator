#pragma once

#include "Memory.h"

namespace PSX
{

constexpr uint32_t BiosSize = 512 * 1024;

using Bios = Memory<BiosSize>;

bool LoadBios( const char* filename, Bios& bios );

void LogKernalCallA( uint32_t call );
void LogKernalCallB( uint32_t call );
void LogKernalCallC( uint32_t call );
void LogSystemCall( uint32_t arg0 );

}