#pragma once

#include <stdx/assert.h>

#include <memory>

namespace ByteIO
{

class ByteStream
{
public:
	enum class SeekDir
	{
		Beg,
		End,
		Cur
	};

	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;

	using pos_type = size_type;
	using offset_type = difference_type;

	ByteStream() = default;

	ByteStream( const ByteStream& other ) : ByteStream( other.data(), other.size() )
	{
		m_readPos = other.m_readPos;
		m_writePos = other.m_writePos;
	}

	ByteStream( ByteStream&& other ) noexcept
		: m_buffer{ std::move( other.m_buffer ) }
		, m_size{ std::exchange( other.m_size, 0 ) }
		, m_capacity{ std::exchange( other.m_capacity, 0 ) }
		, m_readPos{ std::exchange( other.m_readPos, 0 ) }
		, m_writePos{ std::exchange( other.m_writePos, 0 ) }
	{}

	explicit ByteStream( size_type reserveCount )
	{
		Allocate( reserveCount );
	}

	ByteStream( std::unique_ptr<char[]> bytes, size_type count )
		: m_buffer{ std::move( bytes ) }
		, m_size{ count }
		, m_capacity{ count }
	{}

	ByteStream( const char* bytes, size_type count )
	{
		Allocate( count );
		std::copy_n( bytes, count, m_buffer.get() );
		m_size = count;
	}

	template <typename InputIt>
	ByteStream( InputIt first, InputIt last )
	{
		const auto count = std::distance( first, last );
		dbAssert( count >= 0 );
		Allocate( static_cast<size_type>( count ) );
		std::copy( first, last, m_buffer.get() );
		m_size = static_cast<size_type>( count );
	}

	ByteStream& operator=( const ByteStream& other )
	{
		if ( this == &other )
			return *this;

		if ( m_capacity < other.m_size )
			Allocate( other.m_size );

		std::copy_n( other.m_buffer.get(), other.m_size, m_buffer.get() );
		m_size = other.m_size;
		m_readPos = other.m_readPos;
		m_writePos = other.m_writePos;
		return *this;
	}

	ByteStream& operator=( ByteStream&& other ) noexcept
	{
		if ( this == &other )
			return *this;

		m_buffer = std::move( other.m_buffer );
		m_size = std::exchange( other.m_size, 0 );
		m_capacity = std::exchange( other.m_capacity, 0 );
		m_readPos = std::exchange( other.m_readPos, 0 );
		m_writePos = std::exchange( other.m_writePos, 0 );
		return *this;
	}

	bool read( char* bytes, size_type count )
	{
		if ( m_readPos + count > m_size )
			return false;

		std::copy_n( m_buffer.get() + m_readPos, count, bytes );
		m_readPos += count;
		return true;
	}

	void write( const char* bytes, size_type count )
	{
		const size_type requiredCapacity = m_writePos + count;

		if ( m_capacity < requiredCapacity )
			ReserveImp( std::max( m_capacity * 2, requiredCapacity ) );

		std::copy_n( bytes, count, m_buffer.get() + m_writePos );
		m_writePos += count;

		if ( requiredCapacity > m_size )
			m_size = requiredCapacity;
	}

	bool seekg( pos_type pos )
	{
		return seekg( static_cast<offset_type>( pos ), SeekDir::Beg );
	}

	bool seekg( offset_type pos, SeekDir seekdir = SeekDir::Beg )
	{
		return SetPos( m_readPos, pos, seekdir );
	}

	pos_type tellg() const noexcept
	{
		return m_readPos;
	}

	bool seekp( pos_type pos )
	{
		return seekp( static_cast<offset_type>( pos ), SeekDir::Beg );
	}

	bool seekp( offset_type pos, SeekDir seekdir = SeekDir::Beg )
	{
		return SetPos( m_writePos, pos, seekdir );
	}

	pos_type tellp() const noexcept
	{
		return m_writePos;
	}

	void reserve( size_type count )
	{
		if ( count > m_capacity )
			ReserveImp( count );
	}

	char* data() noexcept { return m_buffer.get(); }
	const char* data() const noexcept { return m_buffer.get(); }

	size_type size() const noexcept { return m_size; }
	difference_type ssize() const noexcept { return static_cast<difference_type>( m_size ); }
	size_type capacity() const noexcept { return m_capacity; }

private:
	void ReserveImp( pos_type count )
	{
		dbAssert( count > m_capacity );
		char* newBuffer = new char[ count ];
		std::copy_n( m_buffer.get(), m_size, newBuffer );
		m_capacity = count;
		m_buffer.reset( newBuffer );
	}

	void Allocate( pos_type count )
	{
		if ( count == 0 )
			return;

		m_buffer.reset( new char[ count ] );
		m_capacity = count;
	}

	bool SetPos( pos_type& pos, offset_type offset, SeekDir seekdir ) const noexcept
	{
		offset_type newpos = 0;
		switch ( seekdir )
		{
			case SeekDir::Beg:	newpos = offset;									break;
			case SeekDir::End:	newpos = ssize() - offset;							break;
			case SeekDir::Cur:	newpos = static_cast<offset_type>( pos ) + offset;	break;
		}

		if ( newpos < 0 || newpos > ssize() )
			return false;

		pos = static_cast<pos_type>( newpos );
		return true;
	}

private:
	std::unique_ptr<char[]> m_buffer;
	size_type m_size = 0;
	size_type m_capacity = 0;
	pos_type m_readPos = 0;
	pos_type m_writePos = 0;
};

}