//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: ISteamClient017, ISteamUser019, ISteamUtils008, ISteamFriends015.
//
//============================================================================//

#include "steamclient.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "steam/steamclientpublic.h"
#include "steamapiconfig.h"
#include "steamcm/cmmsg.h"
#include "steamgamecoordinator.h"
#include "stubinterface.h"

extern "C" uint64 SteamStubReturnZero()
{
	return 0;
}

namespace
{

const char *GetCallerModule( void *pReturnAddress )
{
	Dl_info info;
	if ( !dladdr( pReturnAddress, &info ) || !info.dli_fname )
		return "?";
	const char *pchSlash = strrchr( info.dli_fname, '/' );
	return pchSlash ? pchSlash + 1 : info.dli_fname;
}

CStubInterface &GetGenericStub()
{
	static CStubInterface s_interface;
	return s_interface;
}

//-----------------------------------------------------------------------------
// ISteamUser019 (the early slots are stable across SDK versions)
//-----------------------------------------------------------------------------
class CSteamUser
{
public:
	static void InstallMethods( CStubInterface *pInterface )
	{
		pInterface->SetMethod( 0, &GetHSteamUser );
		pInterface->SetMethod( 1, &BLoggedOn );
		pInterface->SetMethod( 2, &GetSteamID );
		pInterface->SetMethod( 13, &GetAuthSessionTicket );
	}

private:
	static int32 GetHSteamUser( void * ) { return 1; }
	static bool BLoggedOn( void * ) { return true; }
	static uint64 GetSteamID( void * ) { return GetLocalSteamID().ConvertToUint64(); }

	// Multiplayer listen servers (auth 3) want a ticket. Nothing validates it.
	static uint32 GetAuthSessionTicket( void *, void *pTicket, int cbMaxTicket, uint32 *pcbTicket )
	{
		const uint64 ulSteamID = GetLocalSteamID().ConvertToUint64();
		const uint32 cubTicket = cbMaxTicket >= int( sizeof( ulSteamID ) ) ? uint32( sizeof( ulSteamID ) ) : 0;
		if ( cubTicket )
			memcpy( pTicket, &ulSteamID, cubTicket );
		if ( pcbTicket )
			*pcbTicket = cubTicket;
		return 1;	// HAuthTicket; 0 would be k_HAuthTicketInvalid
	}
};

//-----------------------------------------------------------------------------
// ISteamUtils008
//-----------------------------------------------------------------------------
class CSteamUtils
{
public:
	static void InstallMethods( CStubInterface *pInterface )
	{
		pInterface->SetMethod( 2, &GetConnectedUniverse );
		pInterface->SetMethod( 9, &GetAppID );
	}

private:
	static int32 GetConnectedUniverse( void * ) { return k_EUniversePublic; }
	static uint32 GetAppID( void * ) { return k_unDotaAppID; }
};

//-----------------------------------------------------------------------------
// ISteamFriends015
//-----------------------------------------------------------------------------
class CSteamFriends
{
public:
	static void InstallMethods( CStubInterface *pInterface )
	{
		pInterface->SetMethod( 0, &GetPersonaName );
		pInterface->SetMethod( 2, &GetPersonaState );
	}

private:
	static const char *GetPersonaName( void * ) { return "Player"; }
	static int32 GetPersonaState( void * ) { return 1; }	// k_EPersonaStateOnline
};

//-----------------------------------------------------------------------------
// ISteamClient017
//-----------------------------------------------------------------------------
class CSteamClient
{
public:
	static void InstallMethods( CStubInterface *pInterface )
	{
		pInterface->SetMethod( 0, &CreateSteamPipe );
		pInterface->SetMethod( 1, &BReleaseSteamPipe );
		pInterface->SetMethod( 2, &ConnectToGlobalUser );
		pInterface->SetMethod( 5, &GetISteamUser );
		pInterface->SetMethod( 6, &GetISteamGameServer );
		pInterface->SetMethod( 8, &GetISteamFriends );
		pInterface->SetMethod( 9, &GetISteamUtils );
		pInterface->SetMethod( 12, &GetISteamGenericInterface );

		// CSteamAPIContext::Init fails if any of these returns null.
		for ( int iSlot : { 10, 11, 13, 15, 16, 17, 18, 23, 24, 25, 26, 27, 28, 29, 30, 34, 35 } )
			pInterface->SetMethod( iSlot, &GetUnmodeledInterface );
	}

private:
	static int32 CreateSteamPipe( void * ) { return 1; }
	static bool BReleaseSteamPipe( void *, int32 ) { return true; }
	static int32 ConnectToGlobalUser( void *, int32 ) { return 1; }

	static void *GetISteamUser( void *, int32, int32, const char *pchVersion )
	{
		fprintf( stderr, "[shim] %s asks for %s\n", GetCallerModule( __builtin_return_address( 0 ) ),
			pchVersion ? pchVersion : "?" );
		return GetSteamUserInterface();
	}

	// libserver's GC client needs this in game-server mode. libengine2 gets
	// null, otherwise its listen server tries to validate tickets.
	static void *GetISteamGameServer( void *, int32, int32, const char *pchVersion )
	{
		const char *pchCaller = GetCallerModule( __builtin_return_address( 0 ) );
		const bool bEngine = strcmp( pchCaller, "libengine2.dylib" ) == 0;
		fprintf( stderr, "[shim] %s asks for %s%s\n", pchCaller, pchVersion ? pchVersion : "?", bEngine ? " (refused)" : "" );
		return bEngine ? nullptr : GetGenericStubInterface();
	}

	static void *GetISteamFriends( void *, int32, int32, const char * )
	{
		static CStubInterface s_interface;
		static const bool s_bInstalled = ( CSteamFriends::InstallMethods( &s_interface ), true );
		( void )s_bInstalled;
		return s_interface.GetInterface();
	}

	static void *GetISteamUtils( void *, int32, const char * )
	{
		static CStubInterface s_interface;
		static const bool s_bInstalled = ( CSteamUtils::InstallMethods( &s_interface ), true );
		( void )s_bInstalled;
		return s_interface.GetInterface();
	}

	static void *GetISteamGenericInterface( void *, int32, int32, const char *pchVersion )
	{
		if ( pchVersion && strstr( pchVersion, "GameCoordinator" ) )
		{
			fprintf( stderr, "[shim] %s asks for %s\n", GetCallerModule( __builtin_return_address( 0 ) ), pchVersion );
			return CSteamGameCoordinator::Get().GetInterface();
		}
		return GetGenericStubInterface();
	}

	static void *GetUnmodeledInterface( void * ) { return GetGenericStubInterface(); }
};

} // namespace

void *GetSteamClientInterface()
{
	static CStubInterface s_interface;
	static const bool s_bInstalled = ( CSteamClient::InstallMethods( &s_interface ), true );
	( void )s_bInstalled;
	return s_interface.GetInterface();
}

void *GetSteamUserInterface()
{
	static CStubInterface s_interface;
	static const bool s_bInstalled = ( CSteamUser::InstallMethods( &s_interface ), true );
	( void )s_bInstalled;
	return s_interface.GetInterface();
}

void *GetGenericStubInterface()
{
	return GetGenericStub().GetInterface();
}
