#pragma once

#include <cstdlib>
#include <cstdio>

template <typename... Args>
inline void Log( const Args&... args )
{
	std::printf( args... );
	std::printf( "\n" );
}

template <typename... Args>
inline void LogWarning( const Args&... args )
{
	std::printf( "WARNING: " );
	std::printf( args... );
	std::printf( "\n" );
}

template <typename... Args>
inline void LogError( const Args&... args )
{
	std::fprintf( stderr, "ERROR: " );
	std::fprintf( stderr, args... );
	std::fprintf( stderr, "\n" );
}