//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: CCMClient.
//
//============================================================================//

#include "cmclient.h"

#include <chrono>

#include "cmconnection.h"
#include "cmcrypto.h"

namespace
{

// The next packet, if it is of the expected kind.
bool BReceiveExpected( CCMConnection *pConnection, EMsg eMsg, CCMPacket *pPacket )
{
	std::string payload;
	return pConnection->BReceivePacket( &payload ) && CCMPacket::BParse( payload, pPacket ) &&
		pPacket->GetEMsg() == eMsg;
}

} // namespace

CCMClient::CCMClient() : m_bLoggedOn( false ), m_nSessionID( 0 )
{
}

CCMClient::~CCMClient()
{
	Disconnect();
}

std::shared_ptr< CCMConnection > CCMClient::GetConnection()
{
	std::lock_guard< std::mutex > lock( m_mutexConnection );
	return m_pConnection;
}

bool CCMClient::BFail( std::string strError )
{
	m_strLastError = std::move( strError );
	m_bLoggedOn = false;
	return false;
}

bool CCMClient::BConnect( const CMClientConfig_t &config )
{
	Disconnect();

	std::string strError;
	const int hSocket = CCMConnection::ConnectSocket( config.m_strHost, config.m_usPort, config.m_cMillisecTimeout, &strError );
	if ( hSocket < 0 )
		return BFail( "connect " + config.m_strHost + ":" + std::to_string( config.m_usPort ) + ": " + strError );
	std::shared_ptr< CCMConnection > pConnection = std::make_shared< CCMConnection >( hSocket );
	pConnection->SetReceiveTimeout( config.m_cMillisecTimeout );

	// 1. The server opens with a challenge for the channel key.
	CCMPacket packet;
	MsgChannelEncryptRequest_t request;
	if ( !BReceiveExpected( pConnection.get(), k_EMsgChannelEncryptRequest, &packet ) ||
		!request.ParseFromString( packet.GetBody() ) )
		return BFail( "no ChannelEncryptRequest" );
	if ( request.m_unProtocolVersion != k_unChannelEncryptProtocol || request.m_eUniverse != k_EUniversePublic )
		return BFail( "unexpected channel protocol or universe" );

	// 2. Session key + challenge, RSA-encrypted with the server's public key.
	const std::string sessionKey = CCrypto::GenerateRandomBlock( k_cubSessionKey );
	MsgChannelEncryptResponse_t response;
	if ( !CCrypto::BRSAEncrypt( config.m_serverPublicKey, sessionKey + request.m_challenge, &response.m_encryptedKey ) )
		return BFail( "RSA encryption failed (bad server public key?)" );
	if ( !pConnection->BSendPacket( CCMPacket::SerializeSimple( k_EMsgChannelEncryptResponse, response.SerializeAsString() ) ) )
		return BFail( "send ChannelEncryptResponse" );

	// 3. The server confirms; everything after this is encrypted.
	MsgChannelEncryptResult_t result;
	if ( !BReceiveExpected( pConnection.get(), k_EMsgChannelEncryptResult, &packet ) ||
		!result.ParseFromString( packet.GetBody() ) || result.m_eResult != k_EResultOK )
		return BFail( "channel encryption rejected" );
	pConnection->EnableEncryption( sessionKey );

	// 4. Log on. The server assigns the SteamID.
	MsgHdrProtoBuf_t hdr;
	hdr.m_ulSteamID = CSteamID( 0, k_EUniversePublic, k_EAccountTypeIndividual ).ConvertToUint64();
	CMsgClientLogon logon;
	logon.m_strAccountName = config.m_strAccountName;
	if ( !pConnection->BSendPacket( CCMPacket::SerializeProtoBuf( k_EMsgClientLogon, hdr, logon.SerializeAsString() ) ) )
		return BFail( "send ClientLogon" );

	CMsgClientLogonResponse logonResponse;
	if ( !BReceiveExpected( pConnection.get(), k_EMsgClientLogOnResponse, &packet ) ||
		!logonResponse.ParseFromString( packet.GetBody() ) )
		return BFail( "no ClientLogOnResponse" );
	if ( logonResponse.m_eResult != k_EResultOK )
		return BFail( "logon failed (EResult " + std::to_string( logonResponse.m_eResult ) + ")" );

	m_steamID = CSteamID( packet.GetProtoHdr().m_ulSteamID );
	m_nSessionID = packet.GetProtoHdr().m_nSessionID;
	pConnection->SetReceiveTimeout( 0 );	// from here the heartbeats keep the link alive
	{
		std::lock_guard< std::mutex > lock( m_mutexConnection );
		m_pConnection = pConnection;
	}
	m_bLoggedOn = true;
	m_strLastError.clear();

	const int cSecondsHeartbeat =
		logonResponse.m_nOutOfGameHeartbeatSeconds > 0 ? logonResponse.m_nOutOfGameHeartbeatSeconds : 9;
	m_threadReader = std::thread( &CCMClient::ReaderThread, this, pConnection );
	m_threadHeartbeat = std::thread( &CCMClient::HeartbeatThread, this, pConnection, cSecondsHeartbeat );
	return true;
}

void CCMClient::JoinThreads()
{
	for ( std::thread *pThread : { &m_threadReader, &m_threadHeartbeat } )
	{
		if ( pThread->joinable() && pThread->get_id() != std::this_thread::get_id() )
			pThread->join();
	}
}

void CCMClient::Disconnect()
{
	std::shared_ptr< CCMConnection > pConnection;
	{
		std::lock_guard< std::mutex > lock( m_mutexConnection );
		pConnection.swap( m_pConnection );
	}

	if ( pConnection )
	{
		if ( m_bLoggedOn )
		{
			MsgHdrProtoBuf_t hdr;
			hdr.m_ulSteamID = m_steamID.ConvertToUint64();
			hdr.m_nSessionID = m_nSessionID;
			pConnection->BSendPacket( CCMPacket::SerializeProtoBuf( k_EMsgClientLogOff, hdr, {} ) );
		}
		m_bLoggedOn = false;
		pConnection->Shutdown();
	}

	// The threads exit once the socket is shut down and m_pConnection no longer matches.
	JoinThreads();
}

void CCMClient::SetGCMessageHandler( GCMessageHandler_t handler )
{
	std::lock_guard< std::mutex > lock( m_mutexHandler );
	m_GCMessageHandler = std::move( handler );
}

bool CCMClient::BSendProtoBuf( EMsg eMsg, std::string_view body )
{
	std::shared_ptr< CCMConnection > pConnection = GetConnection();
	if ( !pConnection || !m_bLoggedOn )
		return false;

	MsgHdrProtoBuf_t hdr;
	hdr.m_ulSteamID = m_steamID.ConvertToUint64();
	hdr.m_nSessionID = m_nSessionID;
	if ( eMsg == k_EMsgClientToGC )
		hdr.m_unRoutingAppID = k_unDotaAppID;
	return pConnection->BSendPacket( CCMPacket::SerializeProtoBuf( eMsg, hdr, body ) );
}

bool CCMClient::BSendToGC( uint32 unAppID, uint32 unMsgType, std::string_view payload )
{
	CMsgGCClient msg;
	msg.m_unAppID = unAppID;
	msg.m_unMsgType = unMsgType;
	msg.m_payload = std::string( payload );
	return BSendProtoBuf( k_EMsgClientToGC, msg.SerializeAsString() );
}

bool CCMClient::BDispatchPacket( std::string_view payload, int nMultiDepth )
{
	CCMPacket packet;
	if ( !CCMPacket::BParse( payload, &packet ) )
		return true;

	switch ( packet.GetEMsg() )
	{
	case k_EMsgMulti:
	{
		std::vector< std::string > vecPayloads;
		if ( nMultiDepth > 0 || !BUnpackMultiMessage( packet.GetBody(), &vecPayloads ) )
			return true;
		for ( const std::string &part : vecPayloads )
		{
			if ( !BDispatchPacket( part, nMultiDepth + 1 ) )
				return false;
		}
		return true;
	}

	case k_EMsgClientFromGC:
	{
		CMsgGCClient msg;
		if ( !msg.ParseFromString( packet.GetBody() ) )
			return true;
		GCMessageHandler_t handler;
		{
			std::lock_guard< std::mutex > lock( m_mutexHandler );
			handler = m_GCMessageHandler;
		}
		if ( handler )
			handler( msg.m_unMsgType, std::move( msg.m_payload ) );
		return true;
	}

	case k_EMsgClientLoggedOff:
		return false;

	default:
		return true;
	}
}

void CCMClient::ReaderThread( std::shared_ptr< CCMConnection > pConnection )
{
	std::string payload;
	while ( pConnection->BReceivePacket( &payload ) )
	{
		if ( !BDispatchPacket( payload, 0 ) )
			break;
	}

	std::lock_guard< std::mutex > lock( m_mutexConnection );
	if ( m_pConnection == pConnection )
	{
		m_pConnection.reset();
		m_bLoggedOn = false;
	}
}

void CCMClient::HeartbeatThread( std::shared_ptr< CCMConnection > pConnection, int cSecondsInterval )
{
	using namespace std::chrono;
	steady_clock::time_point timeNext = steady_clock::now() + seconds( cSecondsInterval );
	while ( GetConnection() == pConnection )
	{
		std::this_thread::sleep_for( milliseconds( 250 ) );
		if ( steady_clock::now() < timeNext )
			continue;
		timeNext += seconds( cSecondsInterval );
		if ( !BSendProtoBuf( k_EMsgClientHeartBeat, {} ) )
			break;
	}
}
