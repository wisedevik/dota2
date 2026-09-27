//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: CGCMsg.
//
//============================================================================//

#include "gcmsg.h"

namespace GCSDK
{

namespace
{

void AppendUint32( std::string *pBuf, uint32 unValue )
{
	for ( int i = 0; i < 4; ++i )
		pBuf->push_back( char( ( unValue >> ( 8 * i ) ) & 0xff ) );
}

uint32 ReadUint32( const char *pubData )
{
	const uint8 *pub = reinterpret_cast< const uint8 * >( pubData );
	return uint32( pub[0] ) | ( uint32( pub[1] ) << 8 ) | ( uint32( pub[2] ) << 16 ) | ( uint32( pub[3] ) << 24 );
}

} // namespace

CGCMsg::CGCMsg( uint32 unMsgType, const CGCMsg &msgRequest ) : m_unMsgType( unMsgType )
{
	if ( msgRequest.Hdr().has_job_id_source() )
		m_hdr.set_job_id_target( msgRequest.Hdr().job_id_source() );
	if ( msgRequest.Hdr().has_client_steam_id() )
		m_hdr.set_client_steam_id( msgRequest.Hdr().client_steam_id() );
}

bool CGCMsg::BParse( std::string_view packet, CGCMsg *pMsg )
{
	if ( packet.size() < 8 )
		return false;
	const uint32 unRawType = ReadUint32( packet.data() );
	if ( !( unRawType & k_unGCProtoBufFlag ) )
		return false;
	const uint32 cubHdr = ReadUint32( packet.data() + 4 );
	if ( packet.size() - 8 < cubHdr )
		return false;

	pMsg->m_unMsgType = unRawType & ~k_unGCProtoBufFlag;
	if ( !pMsg->m_hdr.ParseFromArray( packet.data() + 8, int( cubHdr ) ) )
		return false;
	pMsg->m_body.assign( packet.data() + 8 + cubHdr, packet.size() - 8 - cubHdr );
	return true;
}

std::string CGCMsg::SerializeAsString() const
{
	const std::string hdr = m_hdr.SerializeAsString();
	std::string packet;
	packet.reserve( 8 + hdr.size() + m_body.size() );
	AppendUint32( &packet, m_unMsgType | k_unGCProtoBufFlag );
	AppendUint32( &packet, uint32( hdr.size() ) );
	packet += hdr;
	packet += m_body;
	return packet;
}

} // namespace GCSDK
