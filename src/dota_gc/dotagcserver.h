//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Dota Game Coordinator.
//
//============================================================================//

#ifndef DOTAGCSERVER_H
#define DOTAGCSERVER_H
#pragma once

#include <mutex>
#include <vector>

#include "dotalobbymanager.h"
#include "gcsdk/gcmsg.h"
#include "steamcm/igcmessagerouter.h"

// Sent when the hello has no version. 1805 is this client build.
const uint32 k_unDefaultGCVersion = 1805;

class CDOTAGCServer : public IGCMessageRouter
{
public:
	// IGCMessageRouter
	void RouteMessageToGC( CSteamID steamID, uint32 unAppID, uint32 unMsgType, std::string_view packet,
		std::vector< GCPacket_t > *pvecReplies ) override;

	void HandleMessage( uint64 ulSteamID, const GCSDK::CGCMsg &msg, std::vector< GCSDK::CGCMsg > *pvecReplies );

private:
	typedef void ( CDOTAGCServer::*MsgHandler_t )( uint64 ulSteamID, const GCSDK::CGCMsg &msg,
		std::vector< GCSDK::CGCMsg > *pvecReplies );
	struct MsgHandlerEntry_t
	{
		uint32 m_unMsgType;
		MsgHandler_t m_pfnHandler;
	};
	static const MsgHandlerEntry_t s_rgMsgHandlers[];

	void OnClientHello( uint64 ulSteamID, const GCSDK::CGCMsg &msg, std::vector< GCSDK::CGCMsg > *pvecReplies );
	void OnPracticeLobbyCreate( uint64 ulSteamID, const GCSDK::CGCMsg &msg, std::vector< GCSDK::CGCMsg > *pvecReplies );
	void OnPracticeLobbySetDetails( uint64 ulSteamID, const GCSDK::CGCMsg &msg, std::vector< GCSDK::CGCMsg > *pvecReplies );
	void OnPracticeLobbyJoin( uint64 ulSteamID, const GCSDK::CGCMsg &msg, std::vector< GCSDK::CGCMsg > *pvecReplies );
	void OnPracticeLobbyLeave( uint64 ulSteamID, const GCSDK::CGCMsg &msg, std::vector< GCSDK::CGCMsg > *pvecReplies );
	void OnPracticeLobbyLaunch( uint64 ulSteamID, const GCSDK::CGCMsg &msg, std::vector< GCSDK::CGCMsg > *pvecReplies );

	std::mutex m_mutex;		// connections call in from their own threads
	CDOTALobbyManager m_LobbyManager;
};

#endif // DOTAGCSERVER_H
