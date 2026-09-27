//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: GC message with a protobuf header.
//
//============================================================================//

#ifndef GCMSG_H
#define GCMSG_H
#pragma once

#include <string>
#include <string_view>

#include "steammessages.pb.h"	// CMsgProtoBufHeader
#include "tier0/platform.h"

namespace GCSDK
{

// High bit of the message type: the body is protobuf.
const uint32 k_unGCProtoBufFlag = 0x80000000u;

class CGCMsg
{
public:
	CGCMsg() : m_unMsgType( 0 ) {}
	explicit CGCMsg( uint32 unMsgType ) : m_unMsgType( unMsgType ) {}
	// A reply to msgRequest: routed back to its job and its sender.
	CGCMsg( uint32 unMsgType, const CGCMsg &msgRequest );

	// False on a short or garbled packet, or on the struct-header form.
	static bool BParse( std::string_view packet, CGCMsg *pMsg );
	std::string SerializeAsString() const;

	uint32 GetEMsg() const { return m_unMsgType; }
	CMsgProtoBufHeader &Hdr() { return m_hdr; }
	const CMsgProtoBufHeader &Hdr() const { return m_hdr; }
	const std::string &GetBody() const { return m_body; }

	template < typename T >
	bool BParseBody( T *pBody ) const { return pBody->ParseFromString( m_body ); }

	template < typename T >
	void SetBody( const T &body ) { ( void )body.SerializeToString( &m_body ); }

private:
	uint32 m_unMsgType;			// protobuf flag stripped
	CMsgProtoBufHeader m_hdr;
	std::string m_body;
};

} // namespace GCSDK

#endif // GCMSG_H
