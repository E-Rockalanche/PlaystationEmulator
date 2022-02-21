#pragma once

#include "FifoBuffer.h"

#include <ByteIO/ByteStream.h>

#include <Math/Matrix.h>
#include <Math/Vector.h>

#include <stdx/bit.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace PSX
{

namespace Detail
{

template <typename T>
static constexpr bool is_byte_like_v = std::is_trivial_v<T> && sizeof( T ) == 1;

template <typename T>
static constexpr bool is_primitive_v = std::is_arithmetic_v<T> || std::is_enum_v<T>;

}

class SaveStateSerializer
{

public:
	enum class Mode
	{
		Read,
		Write
	};

	SaveStateSerializer( Mode mode, ByteIO::ByteStream& stream ) : m_stream{ &stream }, m_mode{ mode } {}

	bool Reading() const noexcept { return m_mode == Mode::Read; }
	bool Writing() const noexcept { return m_mode == Mode::Write; }

	bool Error() const noexcept { return m_error; }

	bool Header( std::string tag, uint32_t version );

	bool End();

	template <typename T, STDX_requires( Detail::is_primitive_v<T> )
	void operator()( T& value )
	{
		SerializePrimitive( value );
	}

	template <typename T, STDX_requires( Detail::is_byte_like_v<T> )
	void operator()( T* bytes, size_t count )
	{
		SerializeBytes( static_cast<void*>( bytes ), count );
	}

	template <typename T, STDX_requires( !Detail::is_byte_like_v<T> )
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
	void operator()( std::vector<T>& vec );

	template <typename CharT, typename CharTraits>
	void operator()( std::basic_string<CharT, CharTraits>& str );

	template <typename T, size_t N>
	void operator()( FifoBuffer<T, N>& fifo );

	template <typename T>
	void operator()( std::optional<T>& opt );

	template <typename T>
	void operator()( Math::Vector2<T>& v )
	{
		( *this )( v.x );
		( *this )( v.y );
	}

	template <typename T>
	void operator()( Math::Vector3<T>& v )
	{
		( *this )( v.x );
		( *this )( v.y );
		( *this )( v.z );
	}

	template <typename T, size_t H, size_t W>
	void operator()( Math::Matrix<T, H, W>& m )
	{
		( *this )( m.elements );
	}

	template <typename T, STDX_requires( Detail::is_primitive_v<T> )
	void SerializePrimitive( T& value )
	{
		if ( Reading() )
			ReadPrimitive( value );
		else
			WritePrimitive( value );
	}

	void SerializeBytes( void* bytes, size_t count );

	template <typename T>
	void SerializeAsBytes( T& value )
	{
		SerializeBytes( &value, sizeof( T ) );
	}

	void SetError()
	{
		dbAssert( Reading() );
		m_error = true;
		dbBreak();
	}

private:
	template <typename T>
	void ReadPrimitive( T& value );

	template <typename T>
	void WritePrimitive( const T& value );

	template <typename T>
	T* GetBuffer( size_t size )
	{
		if ( m_bufferCapacity < sizeof( T ) * size )
			m_buffer.reset( new char[ sizeof( T ) * size ] );

		return reinterpret_cast<T*>( m_buffer.get() );
	}

private:
	ByteIO::ByteStream* m_stream = nullptr;

	// temp buffer to help deserialize into containers
	std::unique_ptr<char[]> m_buffer;
	size_t m_bufferCapacity = 0;

	Mode m_mode;
	bool m_error = false;
};


template <typename T>
void SaveStateSerializer::operator()( std::vector<T>& vec )
{
	uint32_t length = static_cast<uint32_t>( vec.size() );
	( *this )( length );

	if ( Reading() )
		vec.resize( length );

	( *this )( vec.data(), length );
}

template <typename CharT, typename CharTraits>
void SaveStateSerializer::operator()( std::basic_string<CharT, CharTraits>& str )
{
	uint32_t length = static_cast<uint32_t>( str.size() );
	( *this )( length );

	if ( Reading() )
	{
		CharT* buffer = GetBuffer<CharT>( length );
		( *this )( buffer, length );
		str.assign( buffer, length );
	}
	else
	{
		( *this )( str.data(), length );
	}
}

template <typename T, size_t N>
void SaveStateSerializer::operator()( FifoBuffer<T, N>& fifo )
{
	uint32_t length = static_cast<uint32_t>( fifo.Size() );
	( *this )( length );

	if ( Reading() )
	{
		T* buffer = GetBuffer<T>( length );
		( *this )( buffer, length );
		fifo.Clear();
		fifo.Push( buffer, length );
	}
	else
	{
		( *this )( fifo.Data(), length );
	}
}

template <typename T>
void SaveStateSerializer::operator()( std::optional<T>& opt )
{
	bool hasValue = opt.has_value();
	( *this )( hasValue );

	if ( hasValue )
	{
		if ( Reading() )
			opt.emplace();

		( *this )( *opt );
	}
}

template <typename T>
void SaveStateSerializer::ReadPrimitive( T& value )
{
	static_assert( Detail::is_primitive_v<T> );

	dbAssert( Reading() );
	if ( m_error )
		return;

	// read bytes
	std::array<char, sizeof( T )> bytes;
	if ( !m_stream->read( bytes.data(), sizeof( T ) ) )
	{
		SetError();
		return;
	}

	// convert if big endian
	if constexpr ( stdx::endian::native == stdx::endian::little )
		std::memcpy( &value, bytes.data(), sizeof( T ) );
	else
		std::reverse_copy( bytes.begin(), bytes.end(), reinterpret_cast<char*>( &value ) );
}

template <typename T>
void SaveStateSerializer::WritePrimitive( const T& value )
{
	static_assert( Detail::is_primitive_v<T> );

	dbAssert( Writing() );
	std::array<char, sizeof( T )> bytes;

	// convert if big endian
	if constexpr ( stdx::endian::native == stdx::endian::little )
		std::memcpy( bytes.data(), &value, sizeof( T ) );
	else
		std::reverse_copy( reinterpret_cast<const char*>( &value ), reinterpret_cast<const char*>( &value ) + sizeof( T ), bytes.data() );

	// write bytes
	m_stream->write( bytes.data(), sizeof( T ) );
}

}