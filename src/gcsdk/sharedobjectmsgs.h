//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Builders for SO cache subscribe/update/destroy messages.
//
//============================================================================//

#ifndef SHAREDOBJECTMSGS_H
#define SHAREDOBJECTMSGS_H
#pragma once

#include "gcmsg.h"
#include "gcsdk_gcmessages.pb.h"
#include "gcsystemmsgs.pb.h"
#include "sharedobjectcache.h"

namespace GCSDK
{

// k_ESOMsg_CacheSubscribed holding a single object.
template < typename T >
CGCMsg CreateSOCacheSubscribedMsg( ESOType eType, uint64 ulOwnerID, uint64 ulVersion, const T &object )
{
	CMsgSOCacheSubscribed msg;
	msg.set_version( ulVersion );
	CMsgSOIDOwner *pOwner = msg.mutable_owner_soid();
	pOwner->set_type( k_unSOIDTypeSteamID );
	pOwner->set_id( ulOwnerID );
	CMsgSOCacheSubscribed_SubscribedType *pType = msg.add_objects();
	pType->set_type_id( eType );
	pType->add_object_data( object.SerializeAsString() );

	CGCMsg msgOut( k_ESOMsg_CacheSubscribed );
	msgOut.SetBody( msg );
	return msgOut;
}

// k_ESOMsg_Update or k_ESOMsg_Destroy for one object.
template < typename T >
CGCMsg CreateSOSingleObjectMsg( ESOMsg eMsg, ESOType eType, uint64 ulOwnerID, uint64 ulVersion, const T &object )
{
	CMsgSOSingleObject msg;
	msg.set_type_id( eType );
	msg.set_version( ulVersion );
	CMsgSOIDOwner *pOwner = msg.mutable_owner_soid();
	pOwner->set_type( k_unSOIDTypeSteamID );
	pOwner->set_id( ulOwnerID );
	msg.set_object_data( object.SerializeAsString() );

	CGCMsg msgOut( eMsg );
	msgOut.SetBody( msg );
	return msgOut;
}

} // namespace GCSDK

#endif // SHAREDOBJECTMSGS_H
