//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: CCMConnection.
//
//============================================================================//

#include "cmconnection.h"

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "cmcrypto.h"
#include "cmmsg.h"

namespace
{

bool BSendAll( int hSocket, std::string_view data )
{
	size_t cubSent = 0;
	while ( cubSent < data.size() )
	{
		const ssize_t cubResult = send( hSocket, data.data() + cubSent, data.size() - cubSent, 0 );
		if ( cubResult < 0 && errno == EINTR )
			continue;
		if ( cubResult <= 0 )
			return false;
		cubSent += size_t( cubResult );
	}
	return true;
}

bool BReceiveAll( int hSocket, char *pubDest, size_t cubDest )
{
	size_t cubReceived = 0;
	while ( cubReceived < cubDest )
	{
		const ssize_t cubResult = recv( hSocket, pubDest + cubReceived, cubDest - cubReceived, 0 );
		if ( cubResult < 0 && errno == EINTR )
			continue;
		if ( cubResult <= 0 )
			return false;
		cubReceived += size_t( cubResult );
	}
	return true;
}

uint32 ReadUint32( const char *pubData )
{
	const uint8 *pub = reinterpret_cast< const uint8 * >( pubData );
	return uint32( pub[0] ) | ( uint32( pub[1] ) << 8 ) | ( uint32( pub[2] ) << 16 ) | ( uint32( pub[3] ) << 24 );
}

void AppendUint32( std::string *pBuf, uint32 unValue )
{
	for ( int i = 0; i < 4; ++i )
		pBuf->push_back( char( ( unValue >> ( 8 * i ) ) & 0xff ) );
}

} // namespace

CCMConnection::CCMConnection( int hSocket ) : m_hSocket( hSocket )
{
	int nOne = 1;	// a dead peer must not SIGPIPE the host process
	setsockopt( m_hSocket, SOL_SOCKET, SO_NOSIGPIPE, &nOne, sizeof( nOne ) );
}

CCMConnection::~CCMConnection()
{
	if ( m_hSocket >= 0 )
		close( m_hSocket );
}

int CCMConnection::ConnectSocket( const std::string &strHost, uint16 usPort, int cMillisecTimeout, std::string *pError )
{
	addrinfo hints = {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	addrinfo *pResult = nullptr;
	if ( getaddrinfo( strHost.c_str(), std::to_string( usPort ).c_str(), &hints, &pResult ) != 0 || !pResult )
	{
		*pError = "cannot resolve " + strHost;
		return -1;
	}

	const int hSocket = socket( pResult->ai_family, pResult->ai_socktype, pResult->ai_protocol );
	if ( hSocket < 0 )
	{
		freeaddrinfo( pResult );
		*pError = strerror( errno );
		return -1;
	}

	const int nFlags = fcntl( hSocket, F_GETFL, 0 );
	fcntl( hSocket, F_SETFL, nFlags | O_NONBLOCK );
	int nResult = connect( hSocket, pResult->ai_addr, pResult->ai_addrlen );
	freeaddrinfo( pResult );
	if ( nResult < 0 && errno == EINPROGRESS )
	{
		pollfd pfd = { hSocket, POLLOUT, 0 };
		nResult = poll( &pfd, 1, cMillisecTimeout ) == 1 ? 0 : -1;
		int nSocketError = 0;
		socklen_t cubSocketError = sizeof( nSocketError );
		if ( nResult == 0 &&
			( getsockopt( hSocket, SOL_SOCKET, SO_ERROR, &nSocketError, &cubSocketError ) < 0 || nSocketError ) )
		{
			errno = nSocketError;
			nResult = -1;
		}
		else if ( nResult < 0 )
		{
			errno = ETIMEDOUT;
		}
	}

	if ( nResult < 0 )
	{
		*pError = strerror( errno );
		close( hSocket );
		return -1;
	}
	fcntl( hSocket, F_SETFL, nFlags );
	return hSocket;
}

void CCMConnection::SetReceiveTimeout( int cMillisec )
{
	timeval tv = { cMillisec / 1000, ( cMillisec % 1000 ) * 1000 };
	setsockopt( m_hSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof( tv ) );
}

void CCMConnection::EnableEncryption( std::string sessionKey )
{
	m_sessionKey = std::move( sessionKey );
}

bool CCMConnection::BSendPacket( std::string_view payload )
{
	std::lock_guard< std::mutex > lock( m_mutexSend );
	const std::string data = BIsEncrypted()
		? CCrypto::SymmetricEncryptWithHMACIV( payload, m_sessionKey )
		: std::string( payload );
	if ( data.empty() || data.size() > k_cubCMPacketMax )
		return false;

	std::string frame;
	AppendUint32( &frame, uint32( data.size() ) );
	AppendUint32( &frame, k_unCMPacketMagic );
	frame += data;
	return BSendAll( m_hSocket, frame );
}

bool CCMConnection::BReceivePacket( std::string *pPayload )
{
	char rgubHeader[8];
	if ( !BReceiveAll( m_hSocket, rgubHeader, sizeof( rgubHeader ) ) )
		return false;
	const uint32 cubData = ReadUint32( rgubHeader );
	if ( ReadUint32( rgubHeader + 4 ) != k_unCMPacketMagic || cubData > k_cubCMPacketMax )
		return false;

	std::string data( cubData, '\0' );
	if ( !BReceiveAll( m_hSocket, data.data(), cubData ) )
		return false;
	if ( !BIsEncrypted() )
	{
		*pPayload = std::move( data );
		return true;
	}
	return CCrypto::BSymmetricDecryptWithHMACIV( data, m_sessionKey, pPayload );
}

void CCMConnection::Shutdown()
{
	if ( m_hSocket >= 0 )
		shutdown( m_hSocket, SHUT_RDWR );
}
