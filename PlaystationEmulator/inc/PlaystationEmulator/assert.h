#pragma once

#include "compiler.h"

#include <cstdlib>

#ifdef _DEBUG

#include <cstdio>

#define DEBUG true

#define MULTI_LINE_MACRO_BEGIN do{
#define MULTI_LINE_MACRO_END } while( false )

#define dbLog( ... ) do{		\
	std::printf( __VA_ARGS__ );	\
	std::printf( "\n" );		\
} while( false )

#define dbLogToStdErr( ... ) std::fprintf( stderr, __VA_ARGS__ )

#define dbLogErrorLocation() dbLogToStdErr( "ERROR AT %s:%d:\n", __FILE__, __LINE__ )

#define dbLogError( ... )	\
	MULTI_LINE_MACRO_BEGIN	\
	dbLogErrorLocation();	\
	dbLogToStdErr(  __VA_ARGS__ );	\
	dbLogToStdErr( "\n" );	\
	MULTI_LINE_MACRO_END

#define dbLogWarning( ... )	\
	MULTI_LINE_MACRO_BEGIN	\
	dbLogToStdErr( "WARNING AT %s:%d:\n", __FILE__, __LINE__ );	\
	dbLogToStdErr(  __VA_ARGS__ );	\
	dbLogToStdErr( "\n" );	\
	MULTI_LINE_MACRO_END

#define dbBreak() __debugbreak()

#define dbBreakMessage( ... )	\
	MULTI_LINE_MACRO_BEGIN	\
	dbLog( __VA_ARGS__ );	\
	dbBreak();	\
	MULTI_LINE_MACRO_END

#define dbAssertFail()	\
	MULTI_LINE_MACRO_BEGIN	\
		dbLogErrorLocation();	\
		dbLogToStdErr( "Assertion failed" );	\
	MULTI_LINE_MACRO_END

#define dbAssert( condition )	\
	MULTI_LINE_MACRO_BEGIN	\
	if ( !( condition ) ) {	\
		dbAssertFail();	\
		dbBreak();	\
	}	\
	MULTI_LINE_MACRO_END

#define dbAssertMessage( condition, ... )	\
	MULTI_LINE_MACRO_BEGIN	\
	if ( !( condition ) ) {	\
		dbAssertFail();	\
		dbLogToStdErr( __VA_ARGS__ );	\
		dbBreak();	\
	}	\
	MULTI_LINE_MACRO_END

#define dbVerify( condition ) dbAssert( condition )

#define dbVerifyMessage( condition, ... ) dbAssertMessage( condition, __VA_ARGS__ )

// safe to use in constexpr function
#define dbExpects( condition )	\
	MULTI_LINE_MACRO_BEGIN	\
	if ( !( condition ) ) {	\
		dbAssertFail();	\
		dbBreak();	\
	}	\
	MULTI_LINE_MACRO_END

#define dbEnsures( condition )	\
	MULTI_LINE_MACRO_BEGIN	\
	if ( !( condition ) ) {	\
		dbAssertFail();	\
		dbBreak();	\
	}	\
	MULTI_LINE_MACRO_END

#else

#define EmptyBlock do{}while(false)

#define dbLog( ... ) EmptyBlock
#define dbLogToStdErr( ... ) EmptyBlock
#define dbLogErrorLocation() EmptyBlock
#define dbLogError( ... ) EmptyBlock
#define dbLogWarning( ... ) EmptyBlock
#define dbBreak() EmptyBlock
#define dbBreakMessage( ... ) EmptyBlock
#define dbAssert( condition ) STDX_assume( condition )
#define dbAssertMessage( condition, ... ) STDX_assume( condition )
#define dbVerify( condition ) ( condition )
#define dbVerifyMessage( condition, ... ) ( condition )
#define dbExpects( condition ) STDX_assume( condition )
#define dbEnsures( condition ) STDX_assume( condition )

#endif

namespace stdx
{

template <typename... Args>
void fatal_error( const char* format_str, const Args&... args )
{
	std::printf( "fatal error: " );
	std::printf( format_str, args... );
	std::exit( 1 );
}

}