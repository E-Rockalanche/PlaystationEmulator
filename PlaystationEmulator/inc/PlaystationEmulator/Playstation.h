#pragma once

#include "CPU.h"

namespace PSX
{

class Playstation
{
public:
	Playstation()
		: m_memoryMap{ m_ram, m_scratchpad, m_memoryControl, m_bios }
		, m_cpu{ m_memoryMap }
	{}

	bool LoadBios();

	void Tick()
	{
		m_cpu.Tick();
	}

private:
	Bios m_bios;
	Scratchpad m_scratchpad;
	MemoryControl m_memoryControl;
	Ram m_ram;
	MemoryMap m_memoryMap;
	MipsR3000Cpu m_cpu;
};

}