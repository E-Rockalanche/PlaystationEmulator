#include "AudioQueue.h"

void AudioQueue::Destroy()
{
	if ( m_deviceId != 0 )
	{
		SDL_CloseAudioDevice( m_deviceId );
		m_deviceId = 0;
	}
}

bool AudioQueue::Initialize( int frequency, SDL_AudioFormat format, uint8_t channels, uint16_t bufferSize )
{
	if ( channels != 1 && channels != 2 )
	{
		dbLogError( "AudioQueue::AudioQueue -- Invalid number of channels [%u]", (uint32_t)channels );
		return false;
	}

	SDL_AudioSpec request;
	request.freq = frequency;
	request.format = format;
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

	m_queueReservedSize = frequency * channels;
	m_queue = std::make_unique<int16_t[]>( m_queueReservedSize );

	SDL_PauseAudioDevice( m_deviceId, m_paused );

	return true;
}

template <typename DestType>
inline void AudioQueue::FillSamples( DestType* samples, size_t count )
{
	std::unique_lock lock{ m_queueMutex };

	dbLog( "AudioQueue::FillSamples -- Reading samples [%u]", (uint32_t)count );

	if ( m_queueSize < count )
	{
		dbLogWarning( "AudioQueue::FillSamples -- Starving audio device [%u]", (uint32_t)( count - m_queueSize ) );
	}

	const size_t available = std::min( m_queueSize, count );
	const size_t seg1Size = std::min( available, m_queueReservedSize - m_queueFirst );
	const size_t seg2Size = available - seg1Size;

	std::copy_n( m_queue.get() + m_queueFirst, seg1Size, samples );
	std::copy_n( m_queue.get(), seg2Size, samples + seg1Size );
	std::fill_n( samples + available, count - available, DestType( 0 ) );

	m_queueSize -= available;
	m_queueFirst = ( m_queueFirst + available ) % m_queueReservedSize;

	dbLog( "\tqueue size: %u", (uint32_t)m_queueSize );
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
			FillSamples( reinterpret_cast<int16_t*>( buffer ), static_cast<size_t>( bufferLength ) / sizeof( int16_t ) );
			break;
		}

		default:
			dbBreak();
	}
}

void AudioQueue::SetPaused( bool pause )
{
	dbAssert( m_deviceId > 0 );
	if ( m_paused != pause )
	{
		SDL_PauseAudioDevice( m_deviceId, pause );
		m_paused = pause;
	}
}

void AudioQueue::PushSamples( const int16_t* samples, size_t count )
{
	std::unique_lock lock{ m_queueMutex };

	dbLog( "AudioQueue::PushSamples -- Pushing samples [%u]", count );

	const size_t capacity = m_queueReservedSize - m_queueSize;
	if ( capacity < count )
	{
		const size_t dropCount = count - capacity;
		dbLogWarning( "AudioQueue::PushSamples -- Exceeding queue capacity. Dropping %u samples", (uint32_t)dropCount );

		m_queueSize -= dropCount;
		m_queueFirst = ( m_queueFirst + dropCount ) % m_queueReservedSize;
	}

	const size_t seg1Count = std::min( count, m_queueReservedSize - m_queueLast );
	const size_t seg2Count = count - seg1Count;

	std::copy_n( samples, seg1Count, m_queue.get() + m_queueLast );
	std::copy_n( samples + seg1Count, seg2Count, m_queue.get() );

	m_queueSize += count;
	m_queueLast = ( m_queueLast + count ) % m_queueReservedSize;

	dbLog( "\tqueue size: %u", (uint32_t)m_queueSize );
}

void AudioQueue::IgnoreSamples( size_t count )
{
	std::unique_lock lock{ m_queueMutex };

	count = std::min( count, m_queueSize );
	m_queueSize -= count;
	m_queueFirst = ( m_queueFirst + count ) % m_queueReservedSize;
}

void AudioQueue::Clear()
{
	std::unique_lock lock{ m_queueMutex };

	m_queueSize = 0;
	m_queueFirst = 0;
	m_queueLast = 0;
}