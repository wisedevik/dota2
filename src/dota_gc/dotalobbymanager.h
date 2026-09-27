//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Practice lobbies.
//
//============================================================================//

#ifndef DOTALOBBYMANAGER_H
#define DOTALOBBYMANAGER_H
#pragma once

#include <map>

#include "dota_gcmessages_client.pb.h"	// CMsgPracticeLobby*
#include "dota_gcmessages_common.pb.h"	// CSODOTALobby
#include "tier0/platform.h"

class CDOTALobbyManager
{
public:
	CDOTALobbyManager() : m_ulNextLobbyID( 1 ) {}

	// Returns the caller's existing lobby if it already has one.
	CSODOTALobby *CreateLobby( uint64 ulSteamID, const CMsgPracticeLobbyCreate &msg );

	// Only the leader can change details.
	CSODOTALobby *SetLobbyDetails( uint64 ulSteamID, const CMsgPracticeLobbySetDetails &msg );

	CSODOTALobby *JoinLobby( uint64 ulSteamID, uint64 ulLobbyID, DOTAJoinLobbyResult *peResult );

	// *pbDestroyed is set when the lobby became empty.
	bool BLeaveLobby( uint64 ulSteamID, uint64 *pulLobbyID, bool *pbDestroyed );

	CSODOTALobby *LaunchLobby( uint64 ulSteamID );

	CSODOTALobby *FindLobbyForMember( uint64 ulSteamID );

private:
	CSODOTALobby *FindLobby( uint64 ulLobbyID );
	void AddMember( CSODOTALobby *pLobby, uint64 ulSteamID );

	std::map< uint64, CSODOTALobby > m_mapLobbies;
	std::map< uint64, uint64 > m_mapMemberLobby;	// SteamID -> lobby id
	uint64 m_ulNextLobbyID;
};

#endif // DOTALOBBYMANAGER_H
