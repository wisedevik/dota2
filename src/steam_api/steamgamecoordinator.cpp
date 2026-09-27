//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: CSteamGameCoordinator.
//
//============================================================================//

#include "steamgamecoordinator.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "steamapiconfig.h"

namespace
{

const uint32 k_unGCProtoBufFlag = 0x80000000u;

void LogGCMessage( const char *pchDirection, uint32 unMsgType, size_t cubPacket )
{
	fprintf( stderr, "[shim] %s msg=%u%s len=%zu\n", pchDirection, unMsgType & ~k_unGCProtoBufFlag,
		( unMsgType & k_unGCProtoBufFlag ) ? " (proto)" : "", cubPacket );
}

void HexDump( const void *pubData, uint32 cubData )
{
	const uint8 *pub = static_cast< const uint8 * >( pubData );
	for ( uint32 i = 0; i < cubData && i < 64; ++i )
		fprintf( stderr, "%02x%s", pub[i], i + 1 < cubData ? " " : "\n" );
	if ( cubData > 64 )
		fprintf( stderr, "...\n" );
}

} // namespace

CSteamGameCoordinator &CSteamGameCoordinator::Get()
{
	static CSteamGameCoordinator *s_pInstance = new CSteamGameCoordinator;
	return *s_pInstance;
}

CSteamGameCoordinator::CSteamGameCoordinator()
{
	m_interface.SetMethod( 0, &CSteamGameCoordinator::SendMessage );
	m_interface.SetMethod( 1, &CSteamGameCoordinator::IsMessageAvailable );
	m_interface.SetMethod( 2, &CSteamGameCoordinator::RetrieveMessage );
}

void CSteamGameCoordinator::Init()
{
	m_CMClient.SetGCMessageHandler( [this]( uint32 unMsgType, std::string packet )
	{
		OnMessageFromGC( unMsgType, std::move( packet ) );
	} );
	BEnsureLoggedOn();	// like Steam: be logged on before the game asks
}

void CSteamGameCoordinator::Shutdown()
{
	m_CMClient.Disconnect();
}

bool CSteamGameCoordinator::BEnsureLoggedOn()
{
	std::lock_guard< std::mutex > lock( m_mutexLogon );
	if ( m_CMClient.BLoggedOn() )
		return true;
	const std::chrono::steady_clock::time_point timeNow = std::chrono::steady_clock::now();
	if ( m_timeLastLogonAttempt.time_since_epoch().count() &&
		timeNow - m_timeLastLogonAttempt < std::chrono::seconds( 5 ) )
		return false;
	m_timeLastLogonAttempt = timeNow;

	CMClientConfig_t config;
	config.m_strHost = GetSteamAPIConfigString( "DOTA_CM_HOST", "127.0.0.1" );
	config.m_usPort = uint16( atoi( GetSteamAPIConfigString( "DOTA_CM_PORT", "27017" ).c_str() ) );
	config.m_strAccountName = GetLocalAccountName();
	config.m_cMillisecTimeout = 1500;

	const std::string strKeyPath = GetSteamAPIConfigString( "DOTA_CM_PUBKEY", "cm_rsa.pub.der" );
	std::ifstream keyFile( strKeyPath, std::ios::binary );
	config.m_serverPublicKey.assign( std::istreambuf_iterator< char >( keyFile ), {} );
	if ( config.m_serverPublicKey.empty() )
	{
		fprintf( stderr, "[shim] CM: no server public key at %s, staying offline\n", strKeyPath.c_str() );
		return false;
	}

	if ( !m_CMClient.BConnect( config ) )
	{
		fprintf( stderr, "[shim] CM logon to %s:%u failed: %s\n", config.m_strHost.c_str(), config.m_usPort,
			m_CMClient.GetLastError().c_str() );
		return false;
	}
	fprintf( stderr, "[shim] CM: logged on as '%s' (%llu)\n", config.m_strAccountName.c_str(),
		( unsigned long long )m_CMClient.GetSteamID().ConvertToUint64() );
	return true;
}

void CSteamGameCoordinator::OnMessageFromGC( uint32 unMsgType, std::string packet )
{
	LogGCMessage( "GC -> client", unMsgType, packet.size() );
	std::lock_guard< std::mutex > lock( m_mutexInbox );
	m_queueInbox.push_back( { unMsgType, std::move( packet ) } );
}

bool CSteamGameCoordinator::BIsMessageAvailable( uint32 *pcubMsgSize )
{
	std::lock_guard< std::mutex > lock( m_mutexInbox );
	if ( m_queueInbox.empty() )
		return false;
	if ( pcubMsgSize )
		*pcubMsgSize = uint32( m_queueInbox.front().m_packet.size() );
	return true;
}

// pubData goes out as is; the CM stamps our SteamID into its header.
EGCResults CSteamGameCoordinator::SendMessage( void *pThis, uint32 unMsgType, const void *pubData, uint32 cubData )
{
	( void )pThis;
	LogGCMessage( "client -> GC", unMsgType, cubData );
	if ( getenv( "DOTA_GC_HEXDUMP" ) )
		HexDump( pubData, cubData );

	CSteamGameCoordinator &gc = Get();
	if ( gc.BEnsureLoggedOn() &&
		gc.m_CMClient.BSendToGC( k_unDotaAppID, unMsgType, std::string_view( static_cast< const char * >( pubData ), cubData ) ) )
		return k_EGCResultOK;
	return k_EGCResultNotLoggedOn;	// what Steam answers without a CM connection
}

bool CSteamGameCoordinator::IsMessageAvailable( void *pThis, uint32 *pcubMsgSize )
{
	( void )pThis;
	return Get().BIsMessageAvailable( pcubMsgSize );
}

EGCResults CSteamGameCoordinator::RetrieveMessage( void *pThis, uint32 *punMsgType, void *pubDest, uint32 cubDest,
	uint32 *pcubMsgSize )
{
	( void )pThis;
	CSteamGameCoordinator &gc = Get();
	std::lock_guard< std::mutex > lock( gc.m_mutexInbox );
	if ( gc.m_queueInbox.empty() )
		return k_EGCResultNoMessage;

	const PendingMessage_t &msg = gc.m_queueInbox.front();
	if ( pcubMsgSize )
		*pcubMsgSize = uint32( msg.m_packet.size() );
	if ( cubDest < msg.m_packet.size() )
		return k_EGCResultBufferTooSmall;
	if ( punMsgType )
		*punMsgType = msg.m_unMsgType;
	memcpy( pubDest, msg.m_packet.data(), msg.m_packet.size() );
	gc.m_queueInbox.pop_front();
	return k_EGCResultOK;
}
