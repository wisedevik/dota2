//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: gcserver: Steam CM server with the Dota GC behind it.
//
//============================================================================//

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "dota_gc/dotagcserver.h"
#include "steamcm/cmserver.h"

namespace
{

std::string GetPublicKeyPath( const std::string &strPrivateKeyPath )
{
	const std::string strExtension = ".der";
	if ( strPrivateKeyPath.size() > strExtension.size() &&
		strPrivateKeyPath.compare( strPrivateKeyPath.size() - strExtension.size(), strExtension.size(), strExtension ) == 0 )
		return strPrivateKeyPath.substr( 0, strPrivateKeyPath.size() - strExtension.size() ) + ".pub.der";
	return strPrivateKeyPath + ".pub";
}

} // namespace

int main( int argc, char **argv )
{
	uint16 usPort = k_usCMDefaultPort;
	std::string strKeyPath = "build/cm_rsa.der";
	for ( int i = 1; i < argc; ++i )
	{
		if ( !strcmp( argv[i], "-port" ) && i + 1 < argc )
		{
			usPort = uint16( atoi( argv[++i] ) );
		}
		else if ( !strcmp( argv[i], "-key" ) && i + 1 < argc )
		{
			strKeyPath = argv[++i];
		}
		else
		{
			fprintf( stderr, "usage: %s [-port N] [-key PATH]\n", argv[0] );
			return 2;
		}
	}

	std::string privateKey;
	if ( !CCMServer::BLoadOrCreateKey( strKeyPath, GetPublicKeyPath( strKeyPath ), &privateKey ) )
	{
		fprintf( stderr, "cannot load or create the RSA key at %s\n", strKeyPath.c_str() );
		return 1;
	}

	CDOTAGCServer gcServer;
	CCMServer cmServer( privateKey, &gcServer );
	if ( !cmServer.BListen( usPort ) )
		return 1;
	cmServer.RunAcceptLoop();
	return 0;
}
