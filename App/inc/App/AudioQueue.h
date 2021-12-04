#pragma once

#include <stdx/assert.h>

#include <SDL.h>

#include <memory>
#include <mutex>

class AudioQueue
{
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

private:
	template <typename DestType>
	void FillSamples( DestType* samples, size_t count );

	static void StaticFillAudioDeviceBuffer( void* userData, uint8_t* buffer, int lengthBytes );

	void FillAudioDeviceBuffer( uint8_t* buffer, int length );

private:
	SDL_AudioDeviceID m_deviceId = 0;
	SDL_AudioSpec m_settings = {};
	bool m_paused = false;

	std::mutex m_queueMutex;
	std::unique_ptr<int16_t[]> m_queue;
	size_t m_queueReservedSize = 0;
	size_t m_queueSize = 0;
	size_t m_queueFirst = 0;
	size_t m_queueLast = 0;
};