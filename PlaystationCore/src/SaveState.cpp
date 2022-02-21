#include "SaveState.h"

namespace PSX
{

bool SaveStateSerializer::Header( std::string tag, uint32_t version )
{
	if ( Reading() )
	{
		if ( m_error )
			return false;

		std::string t;
		uint32_t v;
		( *this )( t );
		( *this )( v );

		const bool headerOK = ( t == tag && v == version );
		if ( !headerOK )
			SetError();

		return headerOK;
	}
	else
	{
		( *this )( tag );
		( *this )( version );
		return true;
	}
}

bool SaveStateSerializer::End()
{
	if ( Writing() )
		return true;

	if ( m_error )
		return false;

	// return true if we read the entire stream
	const auto pos = m_stream->tellg();
	m_stream->seekg( 0, ByteIO::ByteStream::SeekDir::End );
	if ( pos != m_stream->tellg() )
		SetError();

	return !m_error;
}

void SaveStateSerializer::SerializeBytes( void* bytes, size_t count )
{
	if ( Reading() )
	{
		if ( !m_error && !m_stream->read( static_cast<char*>( bytes ), count ) )
			SetError();
	}
	else
	{
		m_stream->write( static_cast<const char*>( bytes ), count );
	}
}

}