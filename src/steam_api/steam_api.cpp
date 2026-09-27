//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: libsteam_api.dylib replacement: the flat C API.
//
//============================================================================//

#include <cstring>

#include "steamcallbacks.h"
#include "steamclient.h"
#include "steamgamecoordinator.h"
#include "tier0/platform.h"

// The library is built with hidden visibility; only this flat API is exported.
#pragma GCC visibility push( default )

extern "C"
{

//-----------------------------------------------------------------------------
// Lifecycle
//-----------------------------------------------------------------------------
bool SteamAPI_Init()
{
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		s_bInitialized = true;
		CSteamGameCoordinator::Get().Init();
		CSteamCallbackManager::Get().StartConnectedAnnouncements();
	}
	return true;
}

bool SteamAPI_InitSafe()
{
	return SteamAPI_Init();
}

void SteamAPI_Shutdown()
{
	CSteamGameCoordinator::Get().Shutdown();
}

bool SteamAPI_IsSteamRunning()
{
	return true;
}

const char *SteamAPI_GetSteamInstallPath()
{
	return ".";
}

int32 SteamAPI_GetHSteamUser() { return 1; }
int32 SteamAPI_GetHSteamPipe() { return 1; }
// Old names, still imported by libclient.
int32 GetHSteamUser() { return 1; }
int32 GetHSteamPipe() { return 1; }

//-----------------------------------------------------------------------------
// Callbacks
//-----------------------------------------------------------------------------
void SteamAPI_RunCallbacks()
{
	CSteamCallbackManager::Get().RunFrame();
}

void SteamAPI_RegisterCallback( void *pCallback, int iCallback )
{
	CSteamCallbackManager::Get().RegisterCallback( pCallback, iCallback );
}

void SteamAPI_UnregisterCallback( void *pCallback )
{
	CSteamCallbackManager::Get().UnregisterCallback( pCallback );
}

void SteamAPI_RegisterCallResult( void *, uint64 ) {}
void SteamAPI_UnregisterCallResult( void *, uint64 ) {}

//-----------------------------------------------------------------------------
// Crash reporting (no-ops)
//-----------------------------------------------------------------------------
void SteamAPI_SetBreakpadAppID( uint32 ) {}
void SteamAPI_SetMiniDumpComment( const char * ) {}
void SteamAPI_SetTryCatchCallbacks( bool ) {}
void SteamAPI_UseBreakpadCrashHandler( const char *, const char *, const char *, bool, void *, void * ) {}

//-----------------------------------------------------------------------------
// Game server API: the listen server inside the client calls these.
//-----------------------------------------------------------------------------
bool SteamInternal_GameServer_Init( uint32, uint16, uint16, uint16, int, const char * ) { return true; }
void SteamGameServer_RunCallbacks() {}
void SteamGameServer_Shutdown() {}
int32 SteamGameServer_GetHSteamUser() { return 1; }
int32 SteamGameServer_GetHSteamPipe() { return 1; }

//-----------------------------------------------------------------------------
// Interface access
//-----------------------------------------------------------------------------
void *SteamInternal_CreateInterface( const char *pchVersion )
{
	if ( pchVersion && strncmp( pchVersion, "SteamClient", 11 ) == 0 )
		return GetSteamClientInterface();
	return GetGenericStubInterface();
}

void *SteamInternal_FindOrCreateUserInterface( int32, const char *pchVersion )
{
	return SteamInternal_CreateInterface( pchVersion );
}

// CSteamAPIContext: its first member is the ISteamClient.
void *SteamInternal_ContextInit( void *pContext )
{
	if ( pContext )
		*reinterpret_cast< void ** >( pContext ) = GetSteamClientInterface();
	return pContext;
}

} // extern "C"

#pragma GCC visibility pop
