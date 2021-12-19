#include "AudioQueue.h"

namespace PSX
{

AudioQueue::BatchWriter::BatchWriter( AudioQueue& queue ) : m_queue{ queue }, m_lock{ queue.m_queueMutex }
{
	m_start = m_pos = m_queue.m_queue.get() + m_queue.m_last;
	m_batchSize = std::min( m_queue.m_bufferSize - m_queue.m_last, m_queue.m_bufferSize - m_queue.m_size );
}

AudioQueue::BatchWriter::~BatchWriter()
{
	if ( !m_queue.m_paused )
	{
		const size_t count = GetCount();
		dbAssert( count <= m_batchSize );

		m_queue.m_last = ( m_queue.m_last + count ) % m_queue.m_bufferSize;
		m_queue.m_size += count;

		m_queue.CheckFullBuffer();
	}

	// release lock
}

void AudioQueue::Destroy()
{
	if ( m_deviceId != 0 )
	{
		SDL_CloseAudioDevice( m_deviceId );
		m_deviceId = 0;
	}
}

bool AudioQueue::Initialize( int frequency, uint8_t channels, uint16_t bufferSize )
{
	if ( channels != 1 && channels != 2 )
	{
		dbLogError( "AudioQueue::AudioQueue -- Invalid number of channels [%u]", (uint32_t)channels );
		return false;
	}

	SDL_AudioSpec request;
	request.freq = frequency;
	request.format = AUDIO_S16;
	request.channels = channels;
	request.samples = bufferSize;
	request.callback = &StaticFillAudioDeviceBuffer;
	request.userdata = this;

	SDL_AudioSpec obtained;
	const auto deviceId = SDL_OpenAudioDevice( nullptr, 0, &request, &obtained, 0 );

	if ( deviceId == 0 )
	{
		dbLogError( "AudioQueue::AudioQueue -- Cannot open audio device [%s]", SDL_GetError() );
		return false;
	}

	if ( request.freq != obtained.freq || request.format != obtained.format || request.channels != obtained.channels )
	{
		dbLogError( "AudioQueue::AudioQueue -- Obtained audio settings do not match requested settings" );
		return false;
	}

	m_deviceId = deviceId;
	m_settings = obtained;

	m_bufferSize = static_cast<size_t>( m_settings.freq * m_settings.channels );
	m_queue = std::make_unique<int16_t[]>( m_bufferSize );

	ClearInternal();

	SDL_PauseAudioDevice( m_deviceId, true );

	return true;
}

void AudioQueue::SetPaused( bool pause )
{
	dbAssert( m_deviceId > 0 );
	if ( m_paused != pause )
	{
		std::unique_lock lock{ m_queueMutex };
		ClearInternal();
		m_paused = pause;
	}
}

template <typename DestType>
inline void AudioQueue::ReadSamples( DestType* samples, size_t count )
{
	std::unique_lock lock{ m_queueMutex };

	if ( m_paused )
	{
		std::fill_n( samples, count, DestType( 0 ) );
		return;
	}

	if ( m_size < count )
		dbLogWarning( "AudioQueue::FillSamples -- Starving audio device" );

	const size_t available = std::min( m_size, count );
	const size_t seg1Size = std::min( available, m_bufferSize - m_first );
	const size_t seg2Size = available - seg1Size;

	std::copy_n( m_queue.get() + m_first, seg1Size, samples );
	std::copy_n( m_queue.get(), seg2Size, samples + seg1Size );
	std::fill_n( samples + available, count - available, DestType( 0 ) );

	m_size -= available;
	m_first = ( m_first + available ) % m_bufferSize;
}

void AudioQueue::StaticFillAudioDeviceBuffer( void* userData, uint8_t* buffer, int length )
{
	reinterpret_cast<AudioQueue*>( userData )->FillAudioDeviceBuffer( buffer, length );
}

void AudioQueue::FillAudioDeviceBuffer( uint8_t* buffer, int bufferLength )
{
	switch ( m_settings.format )
	{
		case AUDIO_S16:
		{
			ReadSamples( reinterpret_cast<int16_t*>( buffer ), static_cast<size_t>( bufferLength ) / sizeof( int16_t ) );
			break;
		}

		default:
			dbBreak();
	}
}

void AudioQueue::PushSamples( const int16_t* samples, size_t count )
{
	std::unique_lock lock{ m_queueMutex };

	const size_t capacity = m_bufferSize - m_size;
	if ( capacity < count )
	{
		const size_t dropCount = count - capacity;
		dbLogWarning( "AudioQueue::PushSamples -- Exceeding queue capacity. Dropping %u samples", (uint32_t)dropCount );

		m_size -= dropCount;
		m_first = ( m_first + dropCount ) % m_bufferSize;
	}

	const size_t seg1Count = std::min( count, m_bufferSize - m_last );
	const size_t seg2Count = count - seg1Count;

	std::copy_n( samples, seg1Count, m_queue.get() + m_last );
	std::copy_n( samples + seg1Count, seg2Count, m_queue.get() );

	m_size += count;
	m_last = ( m_last + count ) % m_bufferSize;

	CheckFullBuffer();
}

void AudioQueue::IgnoreSamples( size_t count )
{
	std::unique_lock lock{ m_queueMutex };

	count = std::min( count, m_size );
	m_size -= count;
	m_first = ( m_first + count ) % m_bufferSize;
}

void AudioQueue::ClearInternal()
{
	m_size = 0;
	m_first = 0;
	m_last = 0;
	m_waitForFullBuffer = true;
	SDL_PauseAudioDevice( m_deviceId, true );
}

void AudioQueue::CheckFullBuffer()
{
	if ( m_waitForFullBuffer && m_size >= static_cast<size_t>( m_settings.samples * m_settings.channels ) )
	{
		m_waitForFullBuffer = false;

		if ( !m_paused )
			SDL_PauseAudioDevice( m_deviceId, false );
	}
}

}