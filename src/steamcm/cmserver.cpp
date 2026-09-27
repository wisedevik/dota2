//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: CCMServer and CCMSession.
//
//============================================================================//

#include "cmserver.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

#include "cmconnection.h"
#include "cmcrypto.h"

namespace
{

bool BReadFile( const std::string &strPath, std::string *pContents )
{
	std::ifstream file( strPath, std::ios::binary );
	if ( !file )
		return false;
	pContents->assign( std::istreambuf_iterator< char >( file ), {} );
	return true;
}

bool BWriteFile( const std::string &strPath, const std::string &contents, mode_t mode )
{
	std::ofstream file( strPath, std::ios::binary | std::ios::trunc );
	if ( !file.write( contents.data(), std::streamsize( contents.size() ) ) )
		return false;
	file.close();
	chmod( strPath.c_str(), mode );
	return true;
}

bool BNegotiateEncryption( CCMConnection *pConnection, const std::string &privateKey, std::string *pSessionKey )
{
	MsgChannelEncryptRequest_t request;
	request.m_challenge = CCrypto::GenerateRandomBlock( k_cubChannelChallenge );
	if ( !pConnection->BSendPacket( CCMPacket::SerializeSimple( k_EMsgChannelEncryptRequest, request.SerializeAsString() ) ) )
		return false;

	std::string payload, plain;
	CCMPacket packet;
	MsgChannelEncryptResponse_t response;
	const bool bOK = pConnection->BReceivePacket( &payload ) && CCMPacket::BParse( payload, &packet ) &&
		!packet.BIsProtoBuf() && packet.GetEMsg() == k_EMsgChannelEncryptResponse &&
		response.ParseFromString( packet.GetBody() ) &&
		CCrypto::BRSADecrypt( privateKey, response.m_encryptedKey, &plain ) &&
		plain.size() == k_cubSessionKey + k_cubChannelChallenge &&
		plain.compare( k_cubSessionKey, std::string::npos, request.m_challenge ) == 0;

	MsgChannelEncryptResult_t result;
	result.m_eResult = bOK ? k_EResultOK : k_EResultFail;
	pConnection->BSendPacket( CCMPacket::SerializeSimple( k_EMsgChannelEncryptResult, result.SerializeAsString() ) );
	if ( !bOK )
		return false;
	*pSessionKey = plain.substr( 0, k_cubSessionKey );
	return true;
}

} // namespace

class CCMSession
{
public:
	CCMSession( CCMConnection *pConnection, IGCMessageRouter *pGCRouter )
		: m_pConnection( pConnection ), m_pGCRouter( pGCRouter ), m_nSessionID( 0 )
	{
	}

	CSteamID GetSteamID() const { return m_steamID; }

	// False when the session is over (the client logged off).
	bool BHandlePacket( std::string_view payload, int nMultiDepth )
	{
		CCMPacket packet;
		if ( !CCMPacket::BParse( payload, &packet ) || !packet.BIsProtoBuf() )
		{
			fprintf( stderr, "[cm] dropping malformed packet (%zu bytes)\n", payload.size() );
			return true;
		}

		switch ( packet.GetEMsg() )
		{
		case k_EMsgMulti:
			return BOnMulti( packet, nMultiDepth );
		case k_EMsgClientLogon:
			OnClientLogon( packet );
			return true;
		case k_EMsgClientHeartBeat:
			return true;	// receiving anything resets the timeout
		case k_EMsgClientToGC:
			OnClientToGC( packet );
			return true;
		case k_EMsgClientLogOff:
			OnClientLogOff();
			return false;
		default:
			fprintf( stderr, "[cm] unhandled EMsg %u from %llu\n", packet.GetEMsg(),
				( unsigned long long )m_steamID.ConvertToUint64() );
			return true;
		}
	}

private:
	MsgHdrProtoBuf_t GetReplyHdr() const
	{
		MsgHdrProtoBuf_t hdr;
		hdr.m_ulSteamID = m_steamID.ConvertToUint64();
		hdr.m_nSessionID = m_nSessionID;
		return hdr;
	}

	bool BOnMulti( const CCMPacket &packet, int nMultiDepth )
	{
		std::vector< std::string > vecPayloads;
		if ( nMultiDepth > 0 || !BUnpackMultiMessage( packet.GetBody(), &vecPayloads ) )
		{
			fprintf( stderr, "[cm] bad or nested Multi, dropped\n" );
			return true;
		}
		for ( const std::string &part : vecPayloads )
		{
			if ( !BHandlePacket( part, nMultiDepth + 1 ) )
				return false;
		}
		return true;
	}

	void OnClientLogon( const CCMPacket &packet )
	{
		CMsgClientLogon logon;
		if ( !logon.ParseFromString( packet.GetBody() ) )
			return;
		const std::string strAccountName = logon.m_strAccountName.empty() ? "anonymous" : logon.m_strAccountName;
		m_steamID = GetSteamIDForAccountName( strAccountName );
		m_nSessionID = int32( CCrypto::CRC32( CCrypto::GenerateRandomBlock( 8 ) ) & 0x7fffffff ) | 1;

		CMsgClientLogonResponse response;
		response.m_eResult = k_EResultOK;
		response.m_nOutOfGameHeartbeatSeconds = k_cSecondsHeartbeat;
		response.m_nInGameHeartbeatSeconds = k_cSecondsHeartbeat;
		response.m_rtime32ServerTime = uint32( time( nullptr ) );
		MsgHdrProtoBuf_t hdr = GetReplyHdr();
		hdr.m_eResult = k_EResultOK;
		hdr.m_bHasResult = true;
		m_pConnection->BSendPacket( CCMPacket::SerializeProtoBuf( k_EMsgClientLogOnResponse, hdr, response.SerializeAsString() ) );
		fprintf( stderr, "[cm] logon '%s' -> %llu (session %d, protocol %u)\n", strAccountName.c_str(),
			( unsigned long long )m_steamID.ConvertToUint64(), m_nSessionID, logon.m_unProtocolVersion );
	}

	void OnClientToGC( const CCMPacket &packet )
	{
		if ( !m_steamID.IsValid() )
		{
			fprintf( stderr, "[cm] ClientToGC before logon, dropped\n" );
			return;
		}
		CMsgGCClient msg;
		if ( !msg.ParseFromString( packet.GetBody() ) || msg.m_unAppID != k_unDotaAppID )
		{
			fprintf( stderr, "[cm] ClientToGC for app %u, dropped\n", msg.m_unAppID );
			return;
		}
		if ( !BStampGCPacketSteamID( &msg.m_payload, m_steamID ) )
		{
			fprintf( stderr, "[cm] GC packet type=%u without a protobuf header, dropped\n",
				msg.m_unMsgType & ~k_unEMsgProtoBufFlag );
			return;
		}

		std::vector< GCPacket_t > vecReplies;
		m_pGCRouter->RouteMessageToGC( m_steamID, msg.m_unAppID, msg.m_unMsgType, msg.m_payload, &vecReplies );

		MsgHdrProtoBuf_t hdr = GetReplyHdr();
		hdr.m_unRoutingAppID = k_unDotaAppID;
		std::vector< std::string > vecPayloads;
		for ( GCPacket_t &reply : vecReplies )
		{
			CMsgGCClient out;
			out.m_unMsgType = reply.m_unMsgType;
			out.m_payload = std::move( reply.m_packet );
			out.m_ulSteamID = m_steamID.ConvertToUint64();
			vecPayloads.push_back( CCMPacket::SerializeProtoBuf( k_EMsgClientFromGC, hdr, out.SerializeAsString() ) );
		}

		if ( vecPayloads.size() == 1 )
		{
			m_pConnection->BSendPacket( vecPayloads.front() );
		}
		else if ( vecPayloads.size() > 1 )
		{
			// Several replies to one request go out as one Multi, as Steam batches.
			const std::string multi = PackMultiMessage( vecPayloads );
			m_pConnection->BSendPacket( CCMPacket::SerializeProtoBuf( k_EMsgMulti, GetReplyHdr(), multi ) );
			fprintf( stderr, "[cm] %zu GC replies batched in a Multi (%zu bytes)\n", vecPayloads.size(), multi.size() );
		}
	}

	void OnClientLogOff()
	{
		m_pConnection->BSendPacket( CCMPacket::SerializeProtoBuf( k_EMsgClientLoggedOff, GetReplyHdr(),
			CMsgClientLoggedOff().SerializeAsString() ) );
		fprintf( stderr, "[cm] %llu logged off\n", ( unsigned long long )m_steamID.ConvertToUint64() );
	}

	CCMConnection *m_pConnection;
	IGCMessageRouter *m_pGCRouter;
	CSteamID m_steamID;
	int32 m_nSessionID;
};

//-----------------------------------------------------------------------------
// CCMServer
//-----------------------------------------------------------------------------
CCMServer::CCMServer( std::string privateKey, IGCMessageRouter *pGCRouter )
	: m_privateKey( std::move( privateKey ) ), m_pGCRouter( pGCRouter ), m_hListenSocket( -1 ), m_usPort( 0 )
{
}

bool CCMServer::BListen( uint16 usPort )
{
	m_hListenSocket = socket( AF_INET, SOCK_STREAM, 0 );
	if ( m_hListenSocket < 0 )
	{
		perror( "socket" );
		return false;
	}
	int nOne = 1;
	setsockopt( m_hListenSocket, SOL_SOCKET, SO_REUSEADDR, &nOne, sizeof( nOne ) );

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	addr.sin_port = htons( usPort );
	if ( bind( m_hListenSocket, reinterpret_cast< sockaddr * >( &addr ), sizeof( addr ) ) < 0 ||
		listen( m_hListenSocket, 16 ) < 0 )
	{
		perror( "bind/listen" );
		close( m_hListenSocket );
		m_hListenSocket = -1;
		return false;
	}

	socklen_t cubAddr = sizeof( addr );
	getsockname( m_hListenSocket, reinterpret_cast< sockaddr * >( &addr ), &cubAddr );
	m_usPort = ntohs( addr.sin_port );
	fprintf( stderr, "[cm] listening on 127.0.0.1:%u\n", m_usPort );
	return true;
}

void CCMServer::RunAcceptLoop()
{
	for ( ;; )
	{
		const int hSocket = accept( m_hListenSocket, nullptr, nullptr );
		if ( hSocket < 0 )
		{
			perror( "accept" );
			continue;
		}
		std::thread( &CCMServer::RunSession, this, hSocket ).detach();
	}
}

void CCMServer::RunSession( int hSocket )
{
	CCMConnection connection( hSocket );
	connection.SetReceiveTimeout( 10 * 1000 );

	std::string sessionKey;
	if ( !BNegotiateEncryption( &connection, m_privateKey, &sessionKey ) )
	{
		fprintf( stderr, "[cm] channel encryption failed, dropping connection\n" );
		return;
	}
	connection.EnableEncryption( sessionKey );
	connection.SetReceiveTimeout( 3 * k_cSecondsHeartbeat * 1000 );

	CCMSession session( &connection, m_pGCRouter );
	std::string payload;
	while ( connection.BReceivePacket( &payload ) )
	{
		if ( !session.BHandlePacket( payload, 0 ) )
			return;
	}
	fprintf( stderr, "[cm] session %llu closed (disconnect or %ds without heartbeat)\n",
		( unsigned long long )session.GetSteamID().ConvertToUint64(), 3 * k_cSecondsHeartbeat );
}

bool CCMServer::BLoadOrCreateKey( const std::string &strPrivateKeyPath, const std::string &strPublicKeyPath,
	std::string *pPrivateKey )
{
	if ( BReadFile( strPrivateKeyPath, pPrivateKey ) )
	{
		std::string existingPublicKey, publicKey;
		if ( !BReadFile( strPublicKeyPath, &existingPublicKey ) )
			return CCrypto::BGetRSAPublicKey( *pPrivateKey, &publicKey ) && BWriteFile( strPublicKeyPath, publicKey, 0644 );
		return true;
	}

	// 1024 bits, the size of Steam's own universe key (a 128-byte encrypted blob).
	std::string publicKey;
	if ( !CCrypto::BGenerateRSAKeyPair( 1024, pPrivateKey, &publicKey ) ||
		!BWriteFile( strPrivateKeyPath, *pPrivateKey, 0600 ) || !BWriteFile( strPublicKeyPath, publicKey, 0644 ) )
		return false;
	fprintf( stderr, "[cm] generated a new RSA key: %s (public: %s)\n", strPrivateKeyPath.c_str(), strPublicKeyPath.c_str() );
	return true;
}
