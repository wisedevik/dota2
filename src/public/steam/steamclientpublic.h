//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: EResult, universes, account types and CSteamID from the Steamworks SDK.
//
//============================================================================//

#ifndef STEAMCLIENTPUBLIC_H
#define STEAMCLIENTPUBLIC_H
#pragma once

#include "tier0/platform.h"

enum EResult
{
	k_EResultOK = 1,
	k_EResultFail = 2,
	k_EResultInvalidProtocolVer = 4,
	k_EResultNotLoggedOn = 21,
};

enum EUniverse
{
	k_EUniverseInvalid = 0,
	k_EUniversePublic = 1,
};

enum EAccountType
{
	k_EAccountTypeInvalid = 0,
	k_EAccountTypeIndividual = 1,
};

// The desktop instance of an individual account.
const uint32 k_unSteamUserDesktopInstance = 1;

//-----------------------------------------------------------------------------
// Purpose: A Steam account id, packed the way Steam packs it:
//          [universe:8][account type:4][instance:20][account id:32]
//-----------------------------------------------------------------------------
class CSteamID
{
public:
	CSteamID() : m_ulSteamID( 0 ) {}
	explicit CSteamID( uint64 ulSteamID ) : m_ulSteamID( ulSteamID ) {}
	CSteamID( uint32 unAccountID, EUniverse eUniverse, EAccountType eAccountType )
		: m_ulSteamID( ( uint64( eUniverse ) << 56 ) | ( uint64( eAccountType ) << 52 ) |
			( uint64( k_unSteamUserDesktopInstance ) << 32 ) | unAccountID )
	{
	}

	uint64 ConvertToUint64() const { return m_ulSteamID; }
	uint32 GetAccountID() const { return uint32( m_ulSteamID & 0xffffffffu ); }
	bool IsValid() const { return m_ulSteamID != 0; }

private:
	uint64 m_ulSteamID;
};

#endif // STEAMCLIENTPUBLIC_H
