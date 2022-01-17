#include "AsyncCDRomReader.h"

namespace PSX
{

void AsyncCDRomReader::Initialize( size_t bufferSize )
{
	dbAssert( !m_readerThread.joinable() );

	// initialize buffer
	m_queue.resize( bufferSize );
	ClearSectorQueue();

	// initialize thread
	m_joining = false;
	m_readerThread = std::thread( &AsyncCDRomReader::ReaderThreadMain, this );
}

void AsyncCDRomReader::Shutdown()
{
	{
		std::unique_lock lock{ m_mutex };
		m_joining = true;
	}

	// notify reader thread to join
	m_produceCondition.notify_one();
	m_readerThread.join();
}

void AsyncCDRomReader::Reset()
{
	std::unique_lock lock{ m_mutex };

	m_hasNextPosition = false;
	ClearSectorQueue();
}

void AsyncCDRomReader::SetCDRom( std::unique_ptr<CDRom> cdrom )
{
	Reset();
	m_cdrom = std::move( cdrom );
}

void AsyncCDRomReader::QueueSectorRead( CDRom::LogicalSector position )
{
	dbExpects( m_cdrom );

	if ( m_size > 0 )
	{
		// early out if position is the next sector in queue
		if ( m_queue[ m_first ].position == position )
			return;

		// check if read-ahead sector is for position
		const uint32_t next = ( m_first + 1 ) % m_queue.size();
		if ( m_size > 1 && m_queue[ next ].position == position )
		{
			// pop previous sector
			m_queue[ m_first ].valid = false;
			m_first = next;
			--m_size;

			// queue may have been full. Notify reader thread
			m_produceCondition.notify_one();
			return;
		}
	}

	// queue position and clear read-ahead buffer
	std::unique_lock lock{ m_mutex };
	m_nextPosition = position;
	m_hasNextPosition = true;
	ClearSectorQueue();
	m_produceCondition.notify_one();
}

bool AsyncCDRomReader::WaitForSector()
{
	if ( m_size > 0 )
		return m_queue[ m_first ].valid;

	// wait for sector read
	std::unique_lock lock{ m_mutex };
	m_consumeCondition.wait( lock, [this] { return m_size > 0 || m_seekError; } );

	return !m_seekError && m_queue[ m_first ].valid;
}

void AsyncCDRomReader::ReaderThreadMain()
{
	for (;;)
	{
		std::unique_lock lock{ m_mutex };
		m_produceCondition.wait( lock, [this] { return ( m_hasNextPosition && m_size < m_queue.size() ) || m_joining; } );

		if ( m_joining )
			return;

		dbAssert( m_cdrom );
		dbAssert( m_hasNextPosition );
		dbAssert( m_size < m_queue.size() );

		const uint32_t seekPosition = m_nextPosition;
		const bool seekOk = m_cdrom->GetCurrentSeekPosition() == seekPosition || m_cdrom->Seek( seekPosition );
		m_seekError = !seekOk;
		if ( !seekOk )
		{
			dbLogWarning( "AsyncCDRomReader::ReaderThreadMain -- seek failed" );
			m_hasNextPosition = false;
			m_consumeCondition.notify_one();
			continue;
		}

		auto& entry = m_queue[ m_last ];

		const bool readOk = m_cdrom->ReadSector( entry.sector, entry.subq );
		if ( !readOk )
		{
			dbLogError( "AsyncCDRomReader::ReaderThreadMain -- read failed" );
			dbBreak();
		}

		entry.position = seekPosition;
		entry.valid = true;

		m_last = ( m_last + 1 ) % m_queue.size();
		++m_size;

		m_nextPosition = seekPosition + 1;

		m_consumeCondition.notify_one();
	}
}

}