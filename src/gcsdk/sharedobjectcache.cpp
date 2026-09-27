//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: CSharedObjectCache.
//
//============================================================================//

#include "sharedobjectcache.h"

namespace GCSDK
{

void CSharedObjectCache::BuildCacheSubscribedMsg( CMsgSOCacheSubscribed *pMsg ) const
{
	pMsg->set_version( m_ulVersion );
	CMsgSOIDOwner *pOwner = pMsg->mutable_owner_soid();
	pOwner->set_type( k_unSOIDTypeSteamID );
	pOwner->set_id( m_ulOwnerID );
	for ( const auto &[eType, vecObjects] : m_mapObjects )
	{
		CMsgSOCacheSubscribed_SubscribedType *pType = pMsg->add_objects();
		pType->set_type_id( eType );
		for ( const std::string &object : vecObjects )
			pType->add_object_data( object );
	}
}

} // namespace GCSDK
