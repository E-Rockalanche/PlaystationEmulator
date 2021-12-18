#pragma once

#include <stdx/assert.h>

#include <SDL.h>

#include <memory>
#include <mutex>

namespace PSX
{

class AudioQueue
{
public:

	// helper class to allow writing individual samples in batches
	class BatchWriter
	{
	public:
		BatchWriter( AudioQueue& queue ) : m_queue{ queue }, m_lock{ queue.m_queueMutex }
		{
			m_start = m_pos = m_queue.m_queue.get() + m_queue.m_last;
			m_batchSize = std::min( m_queue.m_bufferSize - m_queue.m_last, m_queue.m_bufferSize - m_queue.m_size );
		}

		~BatchWriter()
		{
			const size_t count = GetCount();
			dbAssert( count <= m_batchSize );

			m_queue.m_last = ( m_queue.m_last + count ) % m_queue.m_bufferSize;
			m_queue.m_size += count;

			// release lock
		}

		size_t GetBatchSize() const { return m_batchSize; }

		void PushSample( int16_t sample )
		{
			dbExpects( GetCount() < m_batchSize );
			*( m_pos++ ) = sample;
		}

		size_t GetCount() const { return static_cast<size_t>( m_pos - m_start ); }

	private:
		AudioQueue& m_queue;
		std::lock_guard<std::mutex> m_lock;
		const int16_t* m_start = nullptr;
		int16_t* m_pos = nullptr;
		size_t m_batchSize = 0;
	};

public:
	AudioQueue() = default;

	~AudioQueue()
	{
		Destroy();
	}

	void Destroy();

	bool Initialize( int frequency, SDL_AudioFormat format, uint8_t channels, uint16_t bufferSize );

	void SetPaused( bool pause );
	bool GetPaused() const { return m_paused; }

	void PushSamples( const int16_t* samples, size_t count );

	void IgnoreSamples( size_t count );

	void Clear();

	BatchWriter GetBatchWriter()
	{
		return BatchWriter{ *this };
	}

	size_t Capacity() const
	{
		std::unique_lock lock{ m_queueMutex };
		return m_bufferSize - m_size;
	}

private:
	template <typename DestType>
	void FillSamples( DestType* samples, size_t count );

	static void StaticFillAudioDeviceBuffer( void* userData, uint8_t* buffer, int lengthBytes );

	void FillAudioDeviceBuffer( uint8_t* buffer, int length );

private:
	SDL_AudioDeviceID m_deviceId = 0;
	SDL_AudioSpec m_settings = {};
	bool m_paused = false;

	mutable std::mutex m_queueMutex;

	std::unique_ptr<int16_t[]> m_queue;
	size_t m_bufferSize = 0;
	size_t m_size = 0;
	size_t m_first = 0;
	size_t m_last = 0;
};

}