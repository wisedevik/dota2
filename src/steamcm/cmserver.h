//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Steam CM server: accepts clients and routes their GC traffic.
//
//============================================================================//

#ifndef CMSERVER_H
#define CMSERVER_H
#pragma once

#include <string>

#include "cmmsg.h"
#include "igcmessagerouter.h"

const int k_cSecondsHeartbeat = 9;		// what Steam hands out too

class CCMServer
{
public:
	// privateKey: the RSA key whose public half clients encrypt with.
	CCMServer( std::string privateKey, IGCMessageRouter *pGCRouter );

	// Binds 127.0.0.1:usPort (0 picks a free port, see GetPort()).
	bool BListen( uint16 usPort );
	uint16 GetPort() const { return m_usPort; }

	// Accept loop, one thread per session. Blocks.
	void RunAcceptLoop();

	// Loads the server's RSA key from strPrivateKeyPath, or generates one and
	// writes both halves when it does not exist yet.
	static bool BLoadOrCreateKey( const std::string &strPrivateKeyPath, const std::string &strPublicKeyPath,
		std::string *pPrivateKey );

private:
	void RunSession( int hSocket );

	std::string m_privateKey;
	IGCMessageRouter *m_pGCRouter;
	int m_hListenSocket;
	uint16 m_usPort;
};

#endif // CMSERVER_H
