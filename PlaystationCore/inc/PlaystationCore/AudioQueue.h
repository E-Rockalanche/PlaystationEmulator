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
		BatchWriter( AudioQueue& queue );
		~BatchWriter();

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
	using SampleType = int16_t;

	static constexpr int DefaultSampleRate = 44100;
	static constexpr uint16_t DefaultBufferSize = 2048;
	static constexpr uint8_t DefaultChannelCount = 2;

public:
	AudioQueue() = default;

	~AudioQueue()
	{
		Destroy();
	}

	void Destroy();

	bool Initialize( int frequency = DefaultSampleRate, uint8_t channels = DefaultChannelCount, uint16_t bufferSize = DefaultBufferSize );

	void SetPaused( bool pause );
	bool GetPaused() const { return m_paused; }

	void PushSamples( const int16_t* samples, size_t count );
	void PushSilenceFrames( size_t count );

	void IgnoreSamples( size_t count );

	void Clear()
	{
		std::unique_lock lock{ m_queueMutex };
		ClearInternal();
	}

	BatchWriter GetBatchWriter()
	{
		return BatchWriter{ *this };
	}

	size_t Capacity() const
	{
		std::unique_lock lock{ m_queueMutex };
		return m_bufferSize - m_size;
	}

	size_t Size() const
	{
		std::unique_lock lock{ m_queueMutex };
		return m_size;
	}

	// returns buffer size in frames (total samples / channels)
	size_t GetDeviceBufferSize() const
	{
		return m_settings.samples;
	}

private:
	template <typename DestType>
	void ReadSamples( DestType* samples, size_t count );

	static void StaticFillAudioDeviceBuffer( void* userData, uint8_t* buffer, int lengthBytes );

	void FillAudioDeviceBuffer( uint8_t* buffer, int length );

	void CheckFullBuffer();

	void ClearInternal();

	// pop samples from queue to output iterator
	template <typename T>
	void PopSamples( T* dest, size_t count );

	// put popped samples back into the queue
	void UnpopSamples( size_t count );

private:
	SDL_AudioDeviceID m_deviceId = 0;
	SDL_AudioSpec m_settings = {};
	bool m_paused = false;
	bool m_waitForFullBuffer = true;

	mutable std::mutex m_queueMutex;

	std::unique_ptr<int16_t[]> m_queue;
	size_t m_bufferSize = 0;
	size_t m_size = 0;
	size_t m_first = 0;
	size_t m_last = 0;
};

}