//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Steam callback registry and dispatch.
//
//============================================================================//

#ifndef STEAMCALLBACKS_H
#define STEAMCALLBACKS_H
#pragma once

#include <cstddef>

const int k_iSteamServersConnectedCallback = 101;		// k_iSteamUserCallbacks + 1
const int k_iUserStatsReceivedCallback = 1101;			// k_iSteamUserStatsCallbacks + 1

class CSteamCallbackManager
{
public:
	static CSteamCallbackManager &Get();

	void RegisterCallback( void *pCallback, int iCallback );
	void UnregisterCallback( void *pCallback );

	// Fires SteamServersConnected_t a few times during startup.
	void StartConnectedAnnouncements();
	// One SteamAPI_RunCallbacks frame.
	void RunFrame();

private:
	CSteamCallbackManager() : m_cCallbacks( 0 ), m_bConnectedDelivered( false ) {}

	void RunLibclientCallbacks( int iCallback, const void *pvParam = nullptr, size_t cubParam = 0 );
	static bool BIsLibclientCallback( void *pCallback );
	static void RunCallback( void *pCallback, const void *pvParam, size_t cubParam );

	struct RegisteredCallback_t
	{
		void *m_pCallback;
		int m_iCallback;
		bool m_bDelivered;
	};

	static const int k_cMaxCallbacks = 128;
	RegisteredCallback_t m_rgCallbacks[k_cMaxCallbacks];
	int m_cCallbacks;
	bool m_bConnectedDelivered;
};

#endif // STEAMCALLBACKS_H
