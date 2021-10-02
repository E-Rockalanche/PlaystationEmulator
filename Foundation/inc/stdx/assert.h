#pragma once

#include <stdx/compiler.h>

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

#ifdef _DEBUG

#define DEBUG true

#define dbLog( ... )	Log( __VA_ARGS__ )
#define dbLogError( ... )	LogError( __VA_ARGS__ )
#define dbLogWarning( ... )	LogWarning( __VA_ARGS__ )

#define dbBreak() __debugbreak()

#define dbBreakMessage( ... )	\
	MULTI_LINE_MACRO_BEGIN	\
	Log( __VA_ARGS__ );	\
	dbBreak();	\
	MULTI_LINE_MACRO_END

#define dbAssert( condition )	\
	MULTI_LINE_MACRO_BEGIN	\
	if ( STDX_unlikely( !( condition ) ) ) {	\
		LogError( "%s:%u Assertion failed: %s", __FILE__, __LINE__, #condition );	\
		dbBreak();	\
	}	\
	MULTI_LINE_MACRO_END

#define dbAssertMessage( condition, ... )	\
	MULTI_LINE_MACRO_BEGIN	\
	if ( STDX_unlikely( !( condition ) ) ) {	\
		LogError( "%s:%u Assertion failed: %s", __FILE__, __LINE__, #condition );	\
		LogError( __VA_ARGS__ );	\
		dbBreak();	\
	}	\
	MULTI_LINE_MACRO_END

// safe to use in constexpr function
#define dbExpects( condition )	\
	MULTI_LINE_MACRO_BEGIN	\
	if ( STDX_unlikely( !( condition ) ) ) {	\
		LogError( "%s:%u Expected pre-condition failed: %s", __FILE__, __LINE__, #condition );	\
		dbBreak();	\
	}	\
	MULTI_LINE_MACRO_END

#define dbEnsures( condition )	\
	MULTI_LINE_MACRO_BEGIN	\
	if ( STDX_unlikely( !( condition ) ) ) {	\
		LogError( "%s:%u Expected post-condition failed: %s", __FILE__, __LINE__, #condition );	\
		dbBreak();	\
	}	\
	MULTI_LINE_MACRO_END

#else

#define dbLog( ... ) EMPTY_BLOCK
#define dbLogError( ... ) EMPTY_BLOCK
#define dbLogWarning( ... ) EMPTY_BLOCK

#define dbBreak() EMPTY_BLOCK
#define dbBreakMessage( ... ) EMPTY_BLOCK
#define dbAssert( condition ) EMPTY_BLOCK
#define dbAssertMessage( condition, ... ) EMPTY_BLOCK
#define dbExpects( condition ) EMPTY_BLOCK
#define dbEnsures( condition ) EMPTY_BLOCK

#endif