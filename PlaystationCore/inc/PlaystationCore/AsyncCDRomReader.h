#pragma once

#include "Defs.h"
#include "CDRom.h"

#include <mutex>
#include <optional>
#include <thread>

namespace PSX
{

class AsyncCDRomReader
{
public:
	struct SectorEntry
	{
		CDRom::LogicalSector position;
		CDRom::Sector sector;
		CDRom::SubQ subq;
		bool valid = false;
	};

public:
	void Initialize( size_t bufferSize = 8 );

	void Shutdown();

	void Reset();

	void SetCDRom( std::unique_ptr<CDRom> cdrom );

	const CDRom* GetCDRom() const noexcept
	{
		return m_cdrom.get();
	}

	void QueueSectorRead( CDRom::LogicalSector position );

	bool WaitForSector();

	const SectorEntry& GetSectorEntry() const noexcept
	{
		dbExpects( m_size > 0 );
		return m_queue[ m_first ];
	}

private:
	void ReaderThreadMain();

	void ClearSectorQueue()
	{
		m_first = 0;
		m_last = 0;
		m_size = 0;
	}

private:
	std::unique_ptr<CDRom> m_cdrom;

	std::thread m_readerThread;
	std::mutex m_mutex;
	std::condition_variable m_produceCondition;
	std::condition_variable m_consumeCondition;

	std::atomic<CDRom::LogicalSector> m_nextPosition;
	std::atomic_bool m_hasNextPosition = false;

	std::vector<SectorEntry> m_queue;
	uint32_t m_first = 0;
	uint32_t m_last = 0;
	std::atomic<uint32_t> m_size = 0;

	bool m_joining = false;

	std::atomic_bool m_seekError = false;
};

}