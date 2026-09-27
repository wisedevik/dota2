//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: steam_api settings.
//
//============================================================================//

#include "steamapiconfig.h"

#include <cstdlib>

#include "steamcm/cmmsg.h"

std::string GetSteamAPIConfigString( const char *pchName, const char *pchDefault )
{
	const char *pchValue = getenv( pchName );
	return pchValue && *pchValue ? pchValue : pchDefault;
}

std::string GetLocalAccountName()
{
	return GetSteamAPIConfigString( "DOTA_ACCOUNT", "player" );
}

CSteamID GetLocalSteamID()
{
	static const CSteamID s_steamID = GetSteamIDForAccountName( GetLocalAccountName() );
	return s_steamID;
}
