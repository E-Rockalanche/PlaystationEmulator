#include <cstdlib>
#include <cstdio>

#define EMPTY_BLOCK do{}while(false)

#define MULTI_LINE_MACRO_BEGIN do{
#define MULTI_LINE_MACRO_END } while( false )

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