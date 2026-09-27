//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Shared object cache.
//
//============================================================================//

#ifndef SHAREDOBJECTCACHE_H
#define SHAREDOBJECTCACHE_H
#pragma once

#include <map>
#include <string>
#include <vector>

#include "gcsdk_gcmessages.pb.h"
#include "tier0/platform.h"

namespace GCSDK
{

enum ESOType
{
	k_ESOTypeEconGameAccountClient = 7,		// TODO: verify in libserver
	k_ESOTypeDOTAParty = 2003,				// TODO: verify
	k_ESOTypeDOTALobby = 2004,				// TODO: verify
	k_ESOTypeDOTAPartyInvite = 2006,		// TODO: verify
};

// CMsgSOIDOwner.type of a cache keyed by SteamID.
const uint32 k_unSOIDTypeSteamID = 1;

class CSharedObjectCache
{
public:
	explicit CSharedObjectCache( uint64 ulOwnerID ) : m_ulOwnerID( ulOwnerID ), m_ulVersion( 1 ) {}

	// Adds one already-serialized object under a type id.
	void AddObject( ESOType eType, std::string serialized )
	{
		m_mapObjects[eType].push_back( std::move( serialized ) );
		++m_ulVersion;
	}

	uint64 GetOwnerID() const { return m_ulOwnerID; }
	uint64 GetVersion() const { return m_ulVersion; }
	bool BIsEmpty() const { return m_mapObjects.empty(); }

	// The snapshot a welcome carries: one SubscribedType per type id.
	void BuildCacheSubscribedMsg( CMsgSOCacheSubscribed *pMsg ) const;

private:
	uint64 m_ulOwnerID;
	uint64 m_ulVersion;
	std::map< ESOType, std::vector< std::string > > m_mapObjects;
};

} // namespace GCSDK

#endif // SHAREDOBJECTCACHE_H
