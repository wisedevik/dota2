//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: CDOTAGCServer.
//
//============================================================================//

#include "dotagcserver.h"

#include <cstdio>

#include "base_gcmessages.pb.h"			// CSOEconGameAccountClient
#include "dota_gcmessages_client.pb.h"
#include "dota_gcmessages_common.pb.h"
#include "econ_gcmessages.pb.h"
#include "gcsdk/sharedobjectcache.h"
#include "gcsdk/sharedobjectmsgs.h"
#include "gcsdk_gcmessages.pb.h"		// CMsgClientHello, CMsgClientWelcome
#include "gcsystemmsgs.pb.h"

using namespace GCSDK;

namespace
{

// Dashboard requests answered with an empty response until we have data.
struct EmptyReply_t
{
	uint32 m_unRequest;
	uint32 m_unResponse;
};

const EmptyReply_t k_rgEmptyReplies[] =
{
	{ k_EMsgGCRequestCrateItems,				k_EMsgGCRequestCrateItemsResponse },
	{ k_EMsgGCRequestStoreSalesData,			k_EMsgGCRequestStoreSalesDataResponse },
	{ k_EMsgRequestLeagueInfo,					k_EMsgResponseLeagueInfo },
	{ k_EMsgGCMatchmakingStatsRequest,			k_EMsgGCMatchmakingStatsResponse },
	{ k_EMsgGCRequestGuildData,					k_EMsgGCGuildData },
	{ k_EMsgGCGetHeroStandings,					k_EMsgGCGetHeroStandingsResponse },
	{ k_EMsgDOTAGetEventPoints,					k_EMsgDOTAGetEventPointsResponse },
	{ k_EMsgDOTAGetWeekendTourneySchedule,		k_EMsgDOTAWeekendTourneySchedule },
	{ k_EMsgClientToGCEmoticonDataRequest,		k_EMsgGCToClientEmoticonData },
	{ k_EMsgClientToGCGetTrophyList,			k_EMsgClientToGCGetTrophyListResponse },
	{ k_EMsgClientToGCGetProfileCard,			k_EMsgClientToGCGetProfileCardResponse },
	{ k_EMsgClientToGCPlayerStatsRequest,		k_EMsgGCToClientPlayerStatsResponse },
	{ k_EMsgClientToGCFindTopSourceTVGames,		k_EMsgGCToClientFindTopSourceTVGamesResponse },
	{ k_EMsgClientToGCMatchesMinimalRequest,	k_EMsgClientToGCMatchesMinimalResponse },
	{ k_EMsgClientToGCGetProfileTickets,		k_EMsgClientToGCGetProfileTicketsResponse },
};

const EmptyReply_t *FindEmptyReply( uint32 unMsgType )
{
	for ( const EmptyReply_t &entry : k_rgEmptyReplies )
	{
		if ( entry.m_unRequest == unMsgType )
			return &entry;
	}
	return nullptr;
}

// Default cache: an econ account eligible for online play.
CSharedObjectCache CreateDefaultPlayerCache( uint64 ulSteamID )
{
	CSharedObjectCache cache( ulSteamID );
	CSOEconGameAccountClient account;
	account.set_eligible_for_online_play( true );
	account.set_trial_account( false );
	cache.AddObject( k_ESOTypeEconGameAccountClient, account.SerializeAsString() );
	return cache;
}

} // namespace

const CDOTAGCServer::MsgHandlerEntry_t CDOTAGCServer::s_rgMsgHandlers[] =
{
	{ k_EMsgGCClientHello,				&CDOTAGCServer::OnClientHello },
	{ k_EMsgGCPracticeLobbyCreate,		&CDOTAGCServer::OnPracticeLobbyCreate },
	{ k_EMsgGCPracticeLobbySetDetails,	&CDOTAGCServer::OnPracticeLobbySetDetails },
	{ k_EMsgGCPracticeLobbyJoin,		&CDOTAGCServer::OnPracticeLobbyJoin },
	{ k_EMsgGCPracticeLobbyLeave,		&CDOTAGCServer::OnPracticeLobbyLeave },
	{ k_EMsgGCPracticeLobbyLaunch,		&CDOTAGCServer::OnPracticeLobbyLaunch },
};

void CDOTAGCServer::RouteMessageToGC( CSteamID steamID, uint32 unAppID, uint32 unMsgType, std::string_view packet,
	std::vector< GCPacket_t > *pvecReplies )
{
	( void )unAppID;	// the CM only routes Dota (570) here
	CGCMsg msg;
	if ( !CGCMsg::BParse( packet, &msg ) )
	{
		fprintf( stderr, "[gc] undecodable GC packet type=%u (%zu bytes)\n", unMsgType & ~k_unGCProtoBufFlag, packet.size() );
		return;
	}

	std::vector< CGCMsg > vecReplies;
	{
		std::lock_guard< std::mutex > lock( m_mutex );
		HandleMessage( steamID.ConvertToUint64(), msg, &vecReplies );
	}
	for ( const CGCMsg &reply : vecReplies )
		pvecReplies->push_back( { reply.GetEMsg() | k_unGCProtoBufFlag, reply.SerializeAsString() } );
}

void CDOTAGCServer::HandleMessage( uint64 ulSteamID, const CGCMsg &msg, std::vector< CGCMsg > *pvecReplies )
{
	if ( const EmptyReply_t *pEmptyReply = FindEmptyReply( msg.GetEMsg() ) )
	{
		fprintf( stderr, "[gc] empty reply %u -> %u\n", pEmptyReply->m_unRequest, pEmptyReply->m_unResponse );
		pvecReplies->emplace_back( pEmptyReply->m_unResponse, msg );
		return;
	}

	for ( const MsgHandlerEntry_t &entry : s_rgMsgHandlers )
	{
		if ( entry.m_unMsgType == msg.GetEMsg() )
		{
			( this->*entry.m_pfnHandler )( ulSteamID, msg, pvecReplies );
			return;
		}
	}

	fprintf( stderr, "[gc] unhandled msg type=%u from %llu\n", msg.GetEMsg(), ( unsigned long long )ulSteamID );
}

void CDOTAGCServer::OnClientHello( uint64 ulSteamID, const CGCMsg &msg, std::vector< CGCMsg > *pvecReplies )
{
	CMsgClientHello hello;
	( void )msg.BParseBody( &hello );
	fprintf( stderr, "[gc] ClientHello from %llu client_version=%u\n", ( unsigned long long )ulSteamID, hello.version() );

	// Echo the client's version so any build is accepted.
	CMsgClientWelcome welcome;
	welcome.set_version( hello.has_version() ? hello.version() : k_unDefaultGCVersion );
	CreateDefaultPlayerCache( ulSteamID ).BuildCacheSubscribedMsg( welcome.add_outofdate_subscribed_caches() );

	CGCMsg &reply = pvecReplies->emplace_back( k_EMsgGCClientWelcome, msg );
	reply.SetBody( welcome );
}

void CDOTAGCServer::OnPracticeLobbyCreate( uint64 ulSteamID, const CGCMsg &msg, std::vector< CGCMsg > *pvecReplies )
{
	CMsgPracticeLobbyCreate create;
	( void )msg.BParseBody( &create );
	const CSODOTALobby *pLobby = m_LobbyManager.CreateLobby( ulSteamID, create );
	fprintf( stderr, "[gc] lobby %llu created by %llu\n", ( unsigned long long )pLobby->lobby_id(),
		( unsigned long long )ulSteamID );

	// Subscribe the creator to the lobby's SO cache.
	pvecReplies->push_back( CreateSOCacheSubscribedMsg( k_ESOTypeDOTALobby, pLobby->lobby_id(), pLobby->lobby_id(), *pLobby ) );
}

void CDOTAGCServer::OnPracticeLobbySetDetails( uint64 ulSteamID, const CGCMsg &msg, std::vector< CGCMsg > *pvecReplies )
{
	CMsgPracticeLobbySetDetails details;
	( void )msg.BParseBody( &details );
	const CSODOTALobby *pLobby = m_LobbyManager.SetLobbyDetails( ulSteamID, details );
	if ( !pLobby )
		return;
	pvecReplies->push_back( CreateSOSingleObjectMsg( k_ESOMsg_Update, k_ESOTypeDOTALobby, pLobby->lobby_id(),
		pLobby->lobby_id(), *pLobby ) );
}

void CDOTAGCServer::OnPracticeLobbyJoin( uint64 ulSteamID, const CGCMsg &msg, std::vector< CGCMsg > *pvecReplies )
{
	CMsgPracticeLobbyJoin join;
	( void )msg.BParseBody( &join );
	DOTAJoinLobbyResult eResult = DOTA_JOIN_RESULT_SUCCESS;
	const CSODOTALobby *pLobby = m_LobbyManager.JoinLobby( ulSteamID, join.lobby_id(), &eResult );

	CMsgPracticeLobbyJoinResponse response;
	response.set_result( eResult );
	CGCMsg &reply = pvecReplies->emplace_back( k_EMsgGCPracticeLobbyJoinResponse, msg );
	reply.SetBody( response );

	// Push the joined lobby to the new member.
	if ( pLobby )
		pvecReplies->push_back( CreateSOCacheSubscribedMsg( k_ESOTypeDOTALobby, pLobby->lobby_id(), pLobby->lobby_id(), *pLobby ) );
}

void CDOTAGCServer::OnPracticeLobbyLeave( uint64 ulSteamID, const CGCMsg &msg, std::vector< CGCMsg > *pvecReplies )
{
	( void )msg;
	uint64 ulLobbyID = 0;
	bool bDestroyed = false;
	if ( !m_LobbyManager.BLeaveLobby( ulSteamID, &ulLobbyID, &bDestroyed ) )
		return;

	CSODOTALobby gone;
	gone.set_lobby_id( ulLobbyID );
	pvecReplies->push_back( CreateSOSingleObjectMsg( k_ESOMsg_Destroy, k_ESOTypeDOTALobby, ulLobbyID, ulLobbyID, gone ) );
}

void CDOTAGCServer::OnPracticeLobbyLaunch( uint64 ulSteamID, const CGCMsg &msg, std::vector< CGCMsg > *pvecReplies )
{
	( void )msg;
	const CSODOTALobby *pLobby = m_LobbyManager.LaunchLobby( ulSteamID );
	if ( !pLobby )
		return;
	fprintf( stderr, "[gc] lobby %llu launching (state=SERVERSETUP)\n", ( unsigned long long )pLobby->lobby_id() );
	pvecReplies->push_back( CreateSOSingleObjectMsg( k_ESOMsg_Update, k_ESOTypeDOTALobby, pLobby->lobby_id(),
		pLobby->lobby_id(), *pLobby ) );
}
