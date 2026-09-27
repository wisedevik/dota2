//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: End-to-end test of the CM server, CM client and GC.
//
//============================================================================//

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <set>
#include <thread>

#include "dota_gc/dotagcserver.h"
#include "dota_gcmessages_client.pb.h"
#include "dota_gcmessages_common.pb.h"
#include "gcsdk/gcmsg.h"
#include "gcsdk_gcmessages.pb.h"
#include "gcsystemmsgs.pb.h"
#include "steamcm/cmclient.h"
#include "steamcm/cmcrypto.h"
#include "steamcm/cmserver.h"

using namespace GCSDK;

namespace
{

int g_cFailures = 0;

void Check( bool bOK, const char *pszWhat )
{
	printf( "%s %s\n", bOK ? "ok  " : "FAIL", pszWhat );
	if ( !bOK )
		++g_cFailures;
}

void SendToGC( CCMClient *pClient, const CGCMsg &msg )
{
	pClient->BSendToGC( k_unDotaAppID, msg.GetEMsg() | k_unGCProtoBufFlag, msg.SerializeAsString() );
}

void TestCrypto()
{
	const std::string key = CCrypto::GenerateRandomBlock( k_cubSessionKey );
	std::string frame = CCrypto::SymmetricEncryptWithHMACIV( "frame", key );
	std::string plain;
	Check( CCrypto::BSymmetricDecryptWithHMACIV( frame, key, &plain ) && plain == "frame", "AES/HMAC-IV round trip" );
	frame[frame.size() - 1] ^= 1;
	Check( !CCrypto::BSymmetricDecryptWithHMACIV( frame, key, &plain ), "tampered frame rejected" );
	Check( CCrypto::CRC32( "123456789" ) == 0xcbf43926, "CRC32 check value" );
}

void TestMulti()
{
	std::vector< std::string > vecOut;
	const std::vector< std::string > vecSmall = { "one", "two" };
	Check( BUnpackMultiMessage( PackMultiMessage( vecSmall ), &vecOut ) && vecOut == vecSmall, "Multi round trip" );

	const std::vector< std::string > vecBig = { std::string( 4000, 'a' ), std::string( 3000, 'b' ) };
	const std::string packed = PackMultiMessage( vecBig );
	CMsgMulti multi;
	Check( multi.ParseFromString( packed ) && multi.m_unSizeUnzipped == 7008 && multi.m_messageBody.size() < 200,
		"big Multi is gzipped" );
	Check( BUnpackMultiMessage( packed, &vecOut ) && vecOut == vecBig, "gzipped Multi round trip" );

	std::string broken = packed;
	broken[broken.size() / 2] ^= 0x55;
	Check( !BUnpackMultiMessage( broken, &vecOut ), "corrupted gzip Multi rejected" );
}

} // namespace

int main()
{
	TestCrypto();
	TestMulti();

	std::string privateKey, publicKey, otherPrivateKey, otherPublicKey;
	Check( CCrypto::BGenerateRSAKeyPair( 1024, &privateKey, &publicKey ) &&
		CCrypto::BGenerateRSAKeyPair( 1024, &otherPrivateKey, &otherPublicKey ), "RSA keygen" );
	if ( g_cFailures )
		return 1;

	CDOTAGCServer gcServer;
	CCMServer cmServer( privateKey, &gcServer );
	Check( cmServer.BListen( 0 ), "server listens" );
	std::thread( [&] { cmServer.RunAcceptLoop(); } ).detach();

	CMClientConfig_t config;
	config.m_usPort = cmServer.GetPort();
	config.m_strAccountName = "tester";

	// Wrong universe key: the server cannot decrypt the session key.
	{
		CMClientConfig_t badConfig = config;
		badConfig.m_serverPublicKey = otherPublicKey;
		CCMClient client;
		Check( !client.BConnect( badConfig ), "client with the wrong server key is refused" );
	}

	config.m_serverPublicKey = publicKey;
	std::mutex mutex;
	std::condition_variable cvWelcome;
	bool bGotWelcome = false;
	CGCMsg msgWelcome;
	uint32 unWelcomeType = 0;

	CCMClient client;
	client.SetGCMessageHandler( [&]( uint32 unMsgType, std::string payload )
	{
		CGCMsg msg;
		if ( !CGCMsg::BParse( payload, &msg ) || msg.GetEMsg() != k_EMsgGCClientWelcome )
			return;
		std::lock_guard< std::mutex > lock( mutex );
		unWelcomeType = unMsgType;
		msgWelcome = msg;
		bGotWelcome = true;
		cvWelcome.notify_all();
	} );
	Check( client.BConnect( config ), "encrypted logon" );
	const CSteamID steamIDExpected = GetSteamIDForAccountName( "tester" );
	Check( client.GetSteamID().ConvertToUint64() == steamIDExpected.ConvertToUint64(), "SteamID derived from the account name" );

	CGCMsg msgHello( k_EMsgGCClientHello );
	msgHello.Hdr().set_job_id_source( 42 );
	CMsgClientHello hello;
	hello.set_version( 1805 );
	msgHello.SetBody( hello );
	SendToGC( &client, msgHello );

	{
		std::unique_lock< std::mutex > lock( mutex );
		const bool bGot = cvWelcome.wait_for( lock, std::chrono::seconds( 5 ), [&] { return bGotWelcome; } );
		Check( bGot, "ClientWelcome came back as ClientFromGC" );
		if ( bGot )
		{
			CMsgClientWelcome welcome;
			( void )msgWelcome.BParseBody( &welcome );
			Check( unWelcomeType == ( k_EMsgGCClientWelcome | k_unGCProtoBufFlag ), "ClientFromGC msgtype carries the protobuf flag" );
			Check( welcome.version() == 1805, "welcome echoes the client version" );
			Check( msgWelcome.Hdr().job_id_target() == 42, "reply routed to the hello's job" );
			Check( msgWelcome.Hdr().client_steam_id() == steamIDExpected.ConvertToUint64(), "CM stamped the sender's SteamID" );
		}
	}

	// Lobby join: two replies, sent as one Multi.
	{
		CGCMsg msgCreate( k_EMsgGCPracticeLobbyCreate );
		msgCreate.SetBody( CMsgPracticeLobbyCreate() );
		SendToGC( &client, msgCreate );

		std::mutex mutexJoin;
		std::condition_variable cvJoin;
		std::set< uint32 > setTypes;
		CCMClient joiner;
		joiner.SetGCMessageHandler( [&]( uint32 unMsgType, std::string )
		{
			std::lock_guard< std::mutex > lock( mutexJoin );
			setTypes.insert( unMsgType & ~k_unGCProtoBufFlag );
			cvJoin.notify_all();
		} );
		CMClientConfig_t joinerConfig = config;
		joinerConfig.m_strAccountName = "joiner";
		Check( joiner.BConnect( joinerConfig ), "second client logs on" );
		std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );	// lobby 1 exists by now

		CGCMsg msgJoin( k_EMsgGCPracticeLobbyJoin );
		CMsgPracticeLobbyJoin join;
		join.set_lobby_id( 1 );		// the first lobby a fresh server creates
		msgJoin.SetBody( join );
		SendToGC( &joiner, msgJoin );

		std::unique_lock< std::mutex > lock( mutexJoin );
		const bool bBoth = cvJoin.wait_for( lock, std::chrono::seconds( 5 ), [&]
		{
			return setTypes.count( k_EMsgGCPracticeLobbyJoinResponse ) && setTypes.count( k_ESOMsg_CacheSubscribed );
		} );
		Check( bBoth, "join response and lobby subscription arrive (sent as one Multi)" );
		lock.unlock();
		joiner.Disconnect();
	}

	client.Disconnect();
	Check( !client.BLoggedOn(), "logged off" );

	printf( "%s\n", g_cFailures ? "FAILED" : "PASSED" );
	return g_cFailures ? 1 : 0;
}
