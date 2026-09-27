//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: TCP connection with Steam CM framing and channel encryption.
//
//============================================================================//

#ifndef CMCONNECTION_H
#define CMCONNECTION_H
#pragma once

#include <mutex>
#include <string>
#include <string_view>

#include "tier0/platform.h"

class CCMConnection
{
public:
	explicit CCMConnection( int hSocket );
	~CCMConnection();
	CCMConnection( const CCMConnection & ) = delete;
	CCMConnection &operator=( const CCMConnection & ) = delete;

	// Opens a TCP connection, bounded by cMillisecTimeout. Returns the socket or -1,
	// with the reason in *pError.
	static int ConnectSocket( const std::string &strHost, uint16 usPort, int cMillisecTimeout,
		std::string *pError );

	int GetSocket() const { return m_hSocket; }
	void SetReceiveTimeout( int cMillisec );

	// Every frame after this is AES-encrypted with the 32-byte session key.
	void EnableEncryption( std::string sessionKey );
	bool BIsEncrypted() const { return !m_sessionKey.empty(); }

	// Thread-safe: frames from different threads never interleave.
	bool BSendPacket( std::string_view payload );
	// One reader at a time. False on EOF, timeout, bad magic or a frame that
	// fails to decrypt.
	bool BReceivePacket( std::string *pPayload );

	void Shutdown();

private:
	int m_hSocket;
	std::string m_sessionKey;
	std::mutex m_mutexSend;
};

#endif // CMCONNECTION_H
