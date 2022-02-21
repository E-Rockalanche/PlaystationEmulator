#pragma once

#include "FifoBuffer.h"

#include <ByteIO/ByteStream.h>

#include <stdx/bit.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace PSX
{

class SaveStateSerializer
{
public:
	enum class Mode
	{
		Read,
		Write
	};

	SaveStateSerializer( Mode mode, ByteIO::ByteStream stream = {} ) : m_stream{ std::move( stream ) }, m_mode{ mode } {}

	ByteIO::ByteStream& GetStream() noexcept { return m_stream; }
	const ByteIO::ByteStream& GetStream() const noexcept { return m_stream; }

	bool Reading() const noexcept { return m_mode == Mode::Read; }
	bool Writing() const noexcept { return m_mode == Mode::Write; }
	bool Error() const noexcept { return m_error; }

	template <typename T, STDX_requires( std::is_integral_v<T> && sizeof( T ) == 1 )
	void operator()( T* bytes, size_t count )
	{
		if ( m_mode == Mode::Read )
		{
			m_error = m_error || !m_stream.read( reinterpret_cast<std::byte*>( bytes ), count );
		}
		else
		{
			m_stream.write( reinterpret_cast<const std::byte*>( bytes ), count );
		}
	}

	template <typename T, STDX_requires( std::is_integral_v<T> || std::is_floating_point_v<T> )
	void operator()( T& value )
	{
		// TODO check endianness
		( *this )( &value, sizeof( T ) );
	}

	template <typename T>
	void operator()( T* elements, size_t length )
	{
		std::for_each_n( elements, length, [this]( T& value ) { ( *this )( value ); } );
	}

	template <typename T, size_t N>
	void operator()( std::array<T, N>& arr )
	{
		( *this )( arr.data(), N );
	}

	template <typename T>
	void operator()( std::vector<T>& vec )
	{
		uint32_t length = static_cast<uint32_t>( vec.size() );
		( *this )( length );
		if ( Reading() )
			vec.resize( length );

		( *this )( vec.data(), length );
	}

	template <typename T, size_t N>
	void operator()( FifoBuffer<T, N>& fifo )
	{
		uint32_t length = static_cast<uint32_t>( fifo.Size() );
		( *this )( length );
		if ( Reading() )
		{
			ResizeBuffer( length );
			( *this )( reinterpret_cast<T*>( m_buffer.get() ), length );
			fifo.Push( reinterpret_cast<const T*>( m_buffer.get() ), length );
		}
		else
		{
			( *this )( fifo.Data(), length );
		}
	}

private:
	void ResizeBuffer( size_t size )
	{
		if ( m_bufferCapacity < size )
			m_buffer.reset( new std::byte[ size ] );
	}

private:
	ByteIO::ByteStream m_stream;
	Mode m_mode;

	// temp buffer to help deserialize into containers
	std::unique_ptr<std::byte[]> m_buffer;
	size_t m_bufferCapacity = 0;

	bool m_error = false;
};

}