//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: CDOTALobbyManager.
//
//============================================================================//

#include "dotalobbymanager.h"

CSODOTALobby *CDOTALobbyManager::FindLobby( uint64 ulLobbyID )
{
	auto it = m_mapLobbies.find( ulLobbyID );
	return it == m_mapLobbies.end() ? nullptr : &it->second;
}

CSODOTALobby *CDOTALobbyManager::FindLobbyForMember( uint64 ulSteamID )
{
	auto it = m_mapMemberLobby.find( ulSteamID );
	return it == m_mapMemberLobby.end() ? nullptr : FindLobby( it->second );
}

void CDOTALobbyManager::AddMember( CSODOTALobby *pLobby, uint64 ulSteamID )
{
	for ( const CDOTALobbyMember &member : pLobby->members() )
	{
		if ( member.id() == ulSteamID )
			return;	// already in
	}

	CDOTALobbyMember *pMember = pLobby->add_members();
	pMember->set_id( ulSteamID );
	pMember->set_team( DOTA_GC_TEAM_PLAYER_POOL );
	pMember->set_slot( pLobby->members_size() );
	m_mapMemberLobby[ulSteamID] = pLobby->lobby_id();
}

CSODOTALobby *CDOTALobbyManager::CreateLobby( uint64 ulSteamID, const CMsgPracticeLobbyCreate &msg )
{
	if ( CSODOTALobby *pExisting = FindLobbyForMember( ulSteamID ) )
		return pExisting;

	const uint64 ulLobbyID = m_ulNextLobbyID++;
	CSODOTALobby &lobby = m_mapLobbies[ulLobbyID];
	lobby.set_lobby_id( ulLobbyID );
	lobby.set_leader_id( ulSteamID );
	lobby.set_lobby_type( CSODOTALobby_LobbyType_PRACTICE );
	lobby.set_state( CSODOTALobby_State_UI );
	if ( msg.has_pass_key() )
		lobby.set_pass_key( msg.pass_key() );
	AddMember( &lobby, ulSteamID );
	return &lobby;
}

CSODOTALobby *CDOTALobbyManager::SetLobbyDetails( uint64 ulSteamID, const CMsgPracticeLobbySetDetails &msg )
{
	CSODOTALobby *pLobby = FindLobbyForMember( ulSteamID );
	if ( !pLobby || pLobby->leader_id() != ulSteamID )
		return nullptr;

	if ( msg.has_game_name() )
		pLobby->set_game_name( msg.game_name() );
	if ( msg.has_game_mode() )
		pLobby->set_game_mode( msg.game_mode() );
	if ( msg.has_server_region() )
		pLobby->set_server_region( msg.server_region() );
	if ( msg.has_allow_cheats() )
		pLobby->set_allow_cheats( msg.allow_cheats() );
	if ( msg.has_fill_with_bots() )
		pLobby->set_fill_with_bots( msg.fill_with_bots() );
	if ( msg.has_pass_key() )
		pLobby->set_pass_key( msg.pass_key() );
	return pLobby;
}

CSODOTALobby *CDOTALobbyManager::JoinLobby( uint64 ulSteamID, uint64 ulLobbyID, DOTAJoinLobbyResult *peResult )
{
	CSODOTALobby *pLobby = FindLobby( ulLobbyID );
	if ( !pLobby )
	{
		*peResult = DOTA_JOIN_RESULT_INVALID_LOBBY;
		return nullptr;
	}
	AddMember( pLobby, ulSteamID );
	*peResult = DOTA_JOIN_RESULT_SUCCESS;
	return pLobby;
}

bool CDOTALobbyManager::BLeaveLobby( uint64 ulSteamID, uint64 *pulLobbyID, bool *pbDestroyed )
{
	auto it = m_mapMemberLobby.find( ulSteamID );
	if ( it == m_mapMemberLobby.end() )
		return false;
	*pulLobbyID = it->second;
	*pbDestroyed = false;
	m_mapMemberLobby.erase( it );

	CSODOTALobby *pLobby = FindLobby( *pulLobbyID );
	if ( !pLobby )
		return true;

	// Rebuild the member list without the leaver.
	CSODOTALobby rebuilt = *pLobby;
	rebuilt.clear_members();
	for ( const CDOTALobbyMember &member : pLobby->members() )
	{
		if ( member.id() != ulSteamID )
			*rebuilt.add_members() = member;
	}
	*pLobby = rebuilt;

	if ( pLobby->members_size() == 0 )
	{
		m_mapLobbies.erase( *pulLobbyID );
		*pbDestroyed = true;
	}
	else if ( pLobby->leader_id() == ulSteamID )
	{
		pLobby->set_leader_id( pLobby->members( 0 ).id() );	// hand leadership over
	}
	return true;
}

CSODOTALobby *CDOTALobbyManager::LaunchLobby( uint64 ulSteamID )
{
	CSODOTALobby *pLobby = FindLobbyForMember( ulSteamID );
	if ( !pLobby || pLobby->leader_id() != ulSteamID )
		return nullptr;

	// TODO: assign a real dedicated server here; for now only advance the state.
	pLobby->set_state( CSODOTALobby_State_SERVERSETUP );
	return pLobby;
}
