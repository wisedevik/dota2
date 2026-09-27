//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: CSteamCallbackManager.
//
//============================================================================//

#include "steamcallbacks.h"

#include <dlfcn.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "steam/isteamgamecoordinator.h"
#include "steamapiconfig.h"
#include "steamcm/cmmsg.h"
#include "steamgamecoordinator.h"

namespace
{

#pragma pack( push, 4 )
// UserStatsReceived_t
struct UserStatsReceived_t
{
	uint64 m_nGameID;
	EResult m_eResult;
	uint64 m_steamIDUser;
};
#pragma pack( pop )

} // namespace

CSteamCallbackManager &CSteamCallbackManager::Get()
{
	static CSteamCallbackManager *s_pInstance = new CSteamCallbackManager;
	return *s_pInstance;
}

void CSteamCallbackManager::RegisterCallback( void *pCallback, int iCallback )
{
	if ( m_cCallbacks < k_cMaxCallbacks )
		m_rgCallbacks[m_cCallbacks++] = { pCallback, iCallback, false };
}

void CSteamCallbackManager::UnregisterCallback( void *pCallback )
{
	for ( int i = 0; i < m_cCallbacks; ++i )
	{
		if ( m_rgCallbacks[i].m_pCallback == pCallback )
			m_rgCallbacks[i].m_pCallback = nullptr;
	}
}

void CSteamCallbackManager::RunCallback( void *pCallback, const void *pvParam, size_t cubParam )
{
	// CCallbackBase vtable slot 0 is Run( void *pvParam ).
	typedef void ( *RunFn_t )( void *pThis, void *pvParam );
	void **ppfnVTable = *reinterpret_cast< void *** >( pCallback );
	char rgubParam[128] = {};	// SteamServersConnected_t is empty
	if ( pvParam )
		memcpy( rgubParam, pvParam, cubParam < sizeof( rgubParam ) ? cubParam : sizeof( rgubParam ) );
	reinterpret_cast< RunFn_t >( ppfnVTable[0] )( pCallback, rgubParam );
}

bool CSteamCallbackManager::BIsLibclientCallback( void *pCallback )
{
	void **ppfnVTable = *reinterpret_cast< void *** >( pCallback );
	Dl_info info;
	return dladdr( ppfnVTable[0], &info ) && info.dli_fname && strstr( info.dli_fname, "libclient" );
}

void CSteamCallbackManager::RunLibclientCallbacks( int iCallback, const void *pvParam, size_t cubParam )
{
	for ( int i = 0; i < m_cCallbacks; ++i )
	{
		const RegisteredCallback_t &callback = m_rgCallbacks[i];
		if ( callback.m_pCallback && callback.m_iCallback == iCallback && BIsLibclientCallback( callback.m_pCallback ) )
			RunCallback( callback.m_pCallback, pvParam, cubParam );
	}
}

void CSteamCallbackManager::StartConnectedAnnouncements()
{
	std::thread( [this]
	{
		for ( int i = 0; i < 40; ++i )	// ~200 ms .. ~8 s
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
			RunLibclientCallbacks( k_iSteamServersConnectedCallback );
		}
		m_bConnectedDelivered = true;
	} ).detach();
}

void CSteamCallbackManager::RunFrame()
{
	static bool s_bLogged = false;
	if ( !s_bLogged )
	{
		fprintf( stderr, "[shim] RunCallbacks reached\n" );
		s_bLogged = true;
	}

	if ( !m_bConnectedDelivered )
	{
		for ( int i = 0; i < m_cCallbacks; ++i )
		{
			if ( m_rgCallbacks[i].m_pCallback && m_rgCallbacks[i].m_iCallback == k_iSteamServersConnectedCallback )
			{
				fprintf( stderr, "[shim] firing SteamServersConnected\n" );
				RunCallback( m_rgCallbacks[i].m_pCallback, nullptr, 0 );
			}
		}
		m_bConnectedDelivered = true;
	}

	// Steam's reply to RequestCurrentStats; the Dota GC client waits for it.
	for ( int i = 0; i < m_cCallbacks; ++i )
	{
		RegisteredCallback_t &callback = m_rgCallbacks[i];
		if ( !callback.m_pCallback || callback.m_iCallback != k_iUserStatsReceivedCallback || callback.m_bDelivered ||
			!BIsLibclientCallback( callback.m_pCallback ) )
			continue;
		callback.m_bDelivered = true;
		UserStatsReceived_t stats = { k_unDotaAppID, k_EResultOK, GetLocalSteamID().ConvertToUint64() };
		fprintf( stderr, "[shim] firing UserStatsReceived\n" );
		RunCallback( callback.m_pCallback, &stats, sizeof( stats ) );
	}

	uint32 cubPending = 0;
	if ( CSteamGameCoordinator::Get().BIsMessageAvailable( &cubPending ) )
	{
		GCMessageAvailable_t available = { cubPending };
		RunLibclientCallbacks( k_iGCMessageAvailableCallback, &available, sizeof( available ) );
	}
}
