//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Fixes the window-creation deadlock on current macOS.
//
//============================================================================//

#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define TT_INFINITE 0xffffffffu		// tier0's "wait forever"

static const unsigned int k_cMillisecSlice = 4;

// The real CThreadSyncObject::Wait( unsigned int ) in libtier0.
extern bool CThreadSyncObject_Wait( void *pThis, unsigned int nMillisecTimeout )
	__asm( "__ZN17CThreadSyncObject4WaitEj" );

static uint64_t Plat_MSTime( void )
{
	return clock_gettime_nsec_np( CLOCK_UPTIME_RAW ) / 1000000u;
}

static bool WaitPumpingRunLoop( void *pThis, unsigned int nMillisecTimeout )
{
	// Wait( 0 ) is a poll, and waits off the main thread never touch AppKit.
	if ( !pthread_main_np() || nMillisecTimeout == 0 )
		return CThreadSyncObject_Wait( pThis, nMillisecTimeout );

	const bool bInfinite = nMillisecTimeout == TT_INFINITE;
	const uint64_t ulDeadline = Plat_MSTime() + nMillisecTimeout;

	// Poll the real wait in short slices, servicing the run loop in between so
	// AppKit's cross-thread window transaction can complete.
	for ( ;; )
	{
		unsigned int nMillisecSlice = k_cMillisecSlice;
		if ( !bInfinite )
		{
			const uint64_t ulNow = Plat_MSTime();
			// One last poll: the signal may have landed while the run loop spun.
			if ( ulNow >= ulDeadline )
				return CThreadSyncObject_Wait( pThis, 0 );
			if ( ulDeadline - ulNow < nMillisecSlice )
				nMillisecSlice = ( unsigned int )( ulDeadline - ulNow );
		}
		if ( CThreadSyncObject_Wait( pThis, nMillisecSlice ) )
			return true;	// signaled
		CFRunLoopRunInMode( kCFRunLoopDefaultMode, nMillisecSlice / 1000.0, false );
	}
}

__attribute__( ( used ) ) static const struct
{
	const void *m_pReplacement;
	const void *m_pOriginal;
} s_InterposeWait __attribute__( ( section( "__DATA,__interpose" ) ) ) =
{
	( const void * )WaitPumpingRunLoop,
	( const void * )CThreadSyncObject_Wait,
};

__attribute__( ( constructor ) ) static void AnnounceInterpose( void )
{
	fprintf( stderr, "[windowfix] CThreadSyncObject::Wait interpose active\n" );
}
