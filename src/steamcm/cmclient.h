//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Steam CM client: channel handshake, logon, heartbeats, GC traffic.
//
//============================================================================//

#ifndef CMCLIENT_H
#define CMCLIENT_H
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "cmmsg.h"

class CCMConnection;

struct CMClientConfig_t
{
	std::string m_strHost = "127.0.0.1";
	uint16 m_usPort = k_usCMDefaultPort;
	std::string m_serverPublicKey;		// PKCS#1 DER: the CM server's "universe key"
	std::string m_strAccountName;
	int m_cMillisecTimeout = 3000;		// for connect, handshake and logon
};

class CCMClient
{
public:
	typedef std::function< void( uint32 unMsgType, std::string payload ) > GCMessageHandler_t;

	CCMClient();
	~CCMClient();
	CCMClient( const CCMClient & ) = delete;
	CCMClient &operator=( const CCMClient & ) = delete;

	// Blocking. On failure see GetLastError().
	bool BConnect( const CMClientConfig_t &config );
	void Disconnect();		// sends ClientLogOff when logged on

	bool BLoggedOn() const { return m_bLoggedOn; }
	CSteamID GetSteamID() const { return m_steamID; }
	const std::string &GetLastError() const { return m_strLastError; }

	void SetGCMessageHandler( GCMessageHandler_t handler );
	bool BSendToGC( uint32 unAppID, uint32 unMsgType, std::string_view payload );

private:
	bool BFail( std::string strError );
	bool BSendProtoBuf( EMsg eMsg, std::string_view body );
	std::shared_ptr< CCMConnection > GetConnection();
	void JoinThreads();

	// False when the server logged us off.
	bool BDispatchPacket( std::string_view payload, int nMultiDepth );
	void ReaderThread( std::shared_ptr< CCMConnection > pConnection );
	void HeartbeatThread( std::shared_ptr< CCMConnection > pConnection, int cSecondsInterval );

	std::mutex m_mutexConnection;	// guards m_pConnection; the threads hold their own reference
	std::shared_ptr< CCMConnection > m_pConnection;
	std::atomic< bool > m_bLoggedOn;
	CSteamID m_steamID;
	int32 m_nSessionID;
	std::string m_strLastError;

	std::mutex m_mutexHandler;
	GCMessageHandler_t m_GCMessageHandler;

	std::thread m_threadReader;
	std::thread m_threadHeartbeat;
};

#endif // CMCLIENT_H
