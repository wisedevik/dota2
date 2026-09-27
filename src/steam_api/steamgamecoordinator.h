//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: ISteamGameCoordinator over the Steam CM connection.
//
//============================================================================//

#ifndef STEAMGAMECOORDINATOR_H
#define STEAMGAMECOORDINATOR_H
#pragma once

#include <chrono>
#include <deque>
#include <mutex>
#include <string>

#include "steam/isteamgamecoordinator.h"
#include "steamcm/cmclient.h"
#include "stubinterface.h"

class CSteamGameCoordinator
{
public:
	// Never destroyed, so no CM thread outlives it.
	static CSteamGameCoordinator &Get();

	void Init();
	void Shutdown();

	void *GetInterface() { return m_interface.GetInterface(); }
	bool BIsMessageAvailable( uint32 *pcubMsgSize );

private:
	CSteamGameCoordinator();

	// Retries at most every 5 seconds.
	bool BEnsureLoggedOn();
	void OnMessageFromGC( uint32 unMsgType, std::string packet );

	// ISteamGameCoordinator001 slots.
	static EGCResults SendMessage( void *pThis, uint32 unMsgType, const void *pubData, uint32 cubData );
	static bool IsMessageAvailable( void *pThis, uint32 *pcubMsgSize );
	static EGCResults RetrieveMessage( void *pThis, uint32 *punMsgType, void *pubDest, uint32 cubDest,
		uint32 *pcubMsgSize );

	struct PendingMessage_t
	{
		uint32 m_unMsgType;
		std::string m_packet;	// whole GC packet: [type][header length][header][body]
	};

	CStubInterface m_interface;
	CCMClient m_CMClient;

	std::mutex m_mutexLogon;
	std::chrono::steady_clock::time_point m_timeLastLogonAttempt;

	std::mutex m_mutexInbox;
	std::deque< PendingMessage_t > m_queueInbox;	// GC -> client, drained by RetrieveMessage
};

#endif // STEAMGAMECOORDINATOR_H
