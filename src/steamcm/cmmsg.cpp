//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Steam CM message encoding.
//
//============================================================================//

#include "cmmsg.h"

#include <zlib.h>

#include "cmcrypto.h"
#include "protobuflite.h"

namespace
{

void AppendUint32( std::string *pBuf, uint32 unValue )
{
	for ( int i = 0; i < 4; ++i )
		pBuf->push_back( char( ( unValue >> ( 8 * i ) ) & 0xff ) );
}

void AppendUint64( std::string *pBuf, uint64 ulValue )
{
	for ( int i = 0; i < 8; ++i )
		pBuf->push_back( char( ( ulValue >> ( 8 * i ) ) & 0xff ) );
}

//-----------------------------------------------------------------------------
// Purpose: Bounds-checked little-endian reads.
//-----------------------------------------------------------------------------
class CByteReader
{
public:
	explicit CByteReader( std::string_view data ) : m_data( data ), m_nPos( 0 ), m_bValid( true ) {}

	uint32 ReadUint32() { return uint32( ReadLittleEndian( 4 ) ); }
	uint64 ReadUint64() { return ReadLittleEndian( 8 ); }

	std::string_view ReadBytes( size_t cubBytes )
	{
		if ( m_data.size() - m_nPos < cubBytes )
		{
			Invalidate();
			return {};
		}
		std::string_view bytes = m_data.substr( m_nPos, cubBytes );
		m_nPos += cubBytes;
		return bytes;
	}

	std::string_view ReadRemaining()
	{
		std::string_view rest = m_data.substr( m_nPos );
		m_nPos = m_data.size();
		return rest;
	}

	bool BIsValid() const { return m_bValid; }
	bool BAtEnd() const { return m_nPos >= m_data.size(); }

private:
	uint64 ReadLittleEndian( size_t cubValue )
	{
		if ( m_data.size() - m_nPos < cubValue )
		{
			Invalidate();
			return 0;
		}
		uint64 ulValue = 0;
		for ( size_t i = 0; i < cubValue; ++i )
			ulValue |= uint64( uint8( m_data[m_nPos + i] ) ) << ( 8 * i );
		m_nPos += cubValue;
		return ulValue;
	}

	void Invalidate()
	{
		m_bValid = false;
		m_nPos = m_data.size();
	}

	std::string_view m_data;
	size_t m_nPos;
	bool m_bValid;
};

std::string SerializeProtoHdr( const MsgHdrProtoBuf_t &hdr )
{
	CProtoBufLiteWriter writer;
	if ( hdr.m_ulSteamID )
		writer.WriteFixed64( 1, hdr.m_ulSteamID );
	if ( hdr.m_nSessionID )
		writer.WriteInt32( 2, hdr.m_nSessionID );
	if ( hdr.m_unRoutingAppID )
		writer.WriteVarint( 3, hdr.m_unRoutingAppID );
	if ( hdr.m_jobIDSource != k_GIDNil )
		writer.WriteFixed64( 10, hdr.m_jobIDSource );
	if ( hdr.m_jobIDTarget != k_GIDNil )
		writer.WriteFixed64( 11, hdr.m_jobIDTarget );
	if ( hdr.m_bHasResult )
		writer.WriteInt32( 13, hdr.m_eResult );
	return writer.GetBuffer();
}

bool BParseProtoHdr( std::string_view data, MsgHdrProtoBuf_t *pHdr )
{
	CProtoBufLiteReader reader( data );
	ProtoBufLiteField_t field;
	while ( reader.BNextField( &field ) )
	{
		switch ( field.m_unField )
		{
		case 1: pHdr->m_ulSteamID = field.m_ulValue; break;
		case 2: pHdr->m_nSessionID = int32( field.m_ulValue ); break;
		case 3: pHdr->m_unRoutingAppID = uint32( field.m_ulValue ); break;
		case 10: pHdr->m_jobIDSource = field.m_ulValue; break;
		case 11: pHdr->m_jobIDTarget = field.m_ulValue; break;
		case 13:
			pHdr->m_eResult = EResult( int32( field.m_ulValue ) );
			pHdr->m_bHasResult = true;
			break;
		default: break;	// unknown fields are fine, as in real protobuf
		}
	}
	return reader.BIsValid();
}

// gzip (not raw zlib) streams: windowBits 15 + 16.
bool BGzip( std::string_view in, std::string *pOut )
{
	z_stream stream = {};
	if ( deflateInit2( &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY ) != Z_OK )
		return false;
	pOut->assign( deflateBound( &stream, uLong( in.size() ) ) + 32, '\0' );
	stream.next_in = reinterpret_cast< Bytef * >( const_cast< char * >( in.data() ) );
	stream.avail_in = uInt( in.size() );
	stream.next_out = reinterpret_cast< Bytef * >( pOut->data() );
	stream.avail_out = uInt( pOut->size() );
	const int nResult = deflate( &stream, Z_FINISH );
	pOut->resize( stream.total_out );
	deflateEnd( &stream );
	return nResult == Z_STREAM_END;
}

bool BGunzip( std::string_view in, size_t cubExpected, std::string *pOut )
{
	if ( cubExpected > k_cubCMPacketMax )
		return false;
	z_stream stream = {};
	if ( inflateInit2( &stream, 15 + 16 ) != Z_OK )
		return false;
	pOut->assign( cubExpected, '\0' );
	stream.next_in = reinterpret_cast< Bytef * >( const_cast< char * >( in.data() ) );
	stream.avail_in = uInt( in.size() );
	stream.next_out = reinterpret_cast< Bytef * >( pOut->data() );
	stream.avail_out = uInt( pOut->size() );
	const int nResult = inflate( &stream, Z_FINISH );
	const size_t cubProduced = stream.total_out;
	inflateEnd( &stream );
	return nResult == Z_STREAM_END && cubProduced == cubExpected;
}

} // namespace

uint32 GetAccountIDForName( std::string_view accountName )
{
	const uint32 unAccountID = CCrypto::CRC32( accountName ) & 0x7fffffffu;
	return unAccountID ? unAccountID : 1;
}

CSteamID GetSteamIDForAccountName( std::string_view accountName )
{
	return CSteamID( GetAccountIDForName( accountName ), k_EUniversePublic, k_EAccountTypeIndividual );
}

//-----------------------------------------------------------------------------
// CCMPacket
//-----------------------------------------------------------------------------
bool CCMPacket::BParse( std::string_view payload, CCMPacket *pPacket )
{
	CByteReader reader( payload );
	const uint32 unRawEMsg = reader.ReadUint32();
	if ( !reader.BIsValid() )
		return false;

	pPacket->m_eMsg = EMsg( unRawEMsg & ~k_unEMsgProtoBufFlag );
	pPacket->m_bProtoBuf = ( unRawEMsg & k_unEMsgProtoBufFlag ) != 0;
	pPacket->m_hdr = MsgHdrProtoBuf_t();
	if ( pPacket->m_bProtoBuf )
	{
		const uint32 cubHdr = reader.ReadUint32();
		std::string_view hdr = reader.ReadBytes( cubHdr );
		if ( !reader.BIsValid() || !BParseProtoHdr( hdr, &pPacket->m_hdr ) )
			return false;
	}
	else
	{
		reader.ReadUint64();	// target job
		reader.ReadUint64();	// source job
		if ( !reader.BIsValid() )
			return false;
	}
	pPacket->m_body = std::string( reader.ReadRemaining() );
	return true;
}

std::string CCMPacket::SerializeProtoBuf( EMsg eMsg, const MsgHdrProtoBuf_t &hdr, std::string_view body )
{
	const std::string hdrBytes = SerializeProtoHdr( hdr );
	std::string payload;
	AppendUint32( &payload, uint32( eMsg ) | k_unEMsgProtoBufFlag );
	AppendUint32( &payload, uint32( hdrBytes.size() ) );
	payload += hdrBytes;
	payload.append( body );
	return payload;
}

std::string CCMPacket::SerializeSimple( EMsg eMsg, std::string_view body )
{
	std::string payload;
	AppendUint32( &payload, uint32( eMsg ) );
	AppendUint64( &payload, k_GIDNil );	// target job
	AppendUint64( &payload, k_GIDNil );	// source job
	payload.append( body );
	return payload;
}

//-----------------------------------------------------------------------------
// Channel encryption (simple messages)
//-----------------------------------------------------------------------------
std::string MsgChannelEncryptRequest_t::SerializeAsString() const
{
	std::string data;
	AppendUint32( &data, m_unProtocolVersion );
	AppendUint32( &data, uint32( m_eUniverse ) );
	data += m_challenge;
	return data;
}

bool MsgChannelEncryptRequest_t::ParseFromString( std::string_view data )
{
	CByteReader reader( data );
	m_unProtocolVersion = reader.ReadUint32();
	m_eUniverse = EUniverse( reader.ReadUint32() );
	m_challenge = std::string( reader.ReadRemaining() );
	return reader.BIsValid();
}

std::string MsgChannelEncryptResponse_t::SerializeAsString() const
{
	std::string data;
	AppendUint32( &data, m_unProtocolVersion );
	AppendUint32( &data, uint32( m_encryptedKey.size() ) );	// key size
	data += m_encryptedKey;
	AppendUint32( &data, CCrypto::CRC32( m_encryptedKey ) );
	AppendUint32( &data, 0 );
	return data;
}

bool MsgChannelEncryptResponse_t::ParseFromString( std::string_view data )
{
	CByteReader reader( data );
	m_unProtocolVersion = reader.ReadUint32();
	const uint32 cubKey = reader.ReadUint32();
	if ( !reader.BIsValid() || cubKey > 1024 )
		return false;
	m_encryptedKey = std::string( reader.ReadBytes( cubKey ) );
	const uint32 unCRC = reader.ReadUint32();
	return reader.BIsValid() && unCRC == CCrypto::CRC32( m_encryptedKey );
}

std::string MsgChannelEncryptResult_t::SerializeAsString() const
{
	std::string data;
	AppendUint32( &data, uint32( m_eResult ) );
	return data;
}

bool MsgChannelEncryptResult_t::ParseFromString( std::string_view data )
{
	CByteReader reader( data );
	m_eResult = EResult( int32( reader.ReadUint32() ) );
	return reader.BIsValid();
}

//-----------------------------------------------------------------------------
// Protobuf bodies
//-----------------------------------------------------------------------------
std::string CMsgClientLogon::SerializeAsString() const
{
	CProtoBufLiteWriter writer;
	writer.WriteVarint( 1, m_unProtocolVersion );
	if ( !m_strAccountName.empty() )
		writer.WriteBytes( 50, m_strAccountName );
	return writer.GetBuffer();
}

bool CMsgClientLogon::ParseFromString( std::string_view data )
{
	*this = CMsgClientLogon();
	m_unProtocolVersion = 0;
	CProtoBufLiteReader reader( data );
	ProtoBufLiteField_t field;
	while ( reader.BNextField( &field ) )
	{
		if ( field.m_unField == 1 )
			m_unProtocolVersion = uint32( field.m_ulValue );
		else if ( field.m_unField == 50 )
			m_strAccountName = std::string( field.m_bytes );
	}
	return reader.BIsValid();
}

std::string CMsgClientLogonResponse::SerializeAsString() const
{
	CProtoBufLiteWriter writer;
	writer.WriteInt32( 1, m_eResult );
	writer.WriteInt32( 2, m_nOutOfGameHeartbeatSeconds );
	writer.WriteInt32( 3, m_nInGameHeartbeatSeconds );
	writer.WriteVarint( 5, m_rtime32ServerTime );
	writer.WriteVarint( 7, m_unCellID );
	return writer.GetBuffer();
}

bool CMsgClientLogonResponse::ParseFromString( std::string_view data )
{
	*this = CMsgClientLogonResponse();
	CProtoBufLiteReader reader( data );
	ProtoBufLiteField_t field;
	while ( reader.BNextField( &field ) )
	{
		switch ( field.m_unField )
		{
		case 1: m_eResult = EResult( int32( field.m_ulValue ) ); break;
		case 2: m_nOutOfGameHeartbeatSeconds = int32( field.m_ulValue ); break;
		case 3: m_nInGameHeartbeatSeconds = int32( field.m_ulValue ); break;
		case 5: m_rtime32ServerTime = uint32( field.m_ulValue ); break;
		case 7: m_unCellID = uint32( field.m_ulValue ); break;
		default: break;
		}
	}
	return reader.BIsValid();
}

std::string CMsgClientLoggedOff::SerializeAsString() const
{
	CProtoBufLiteWriter writer;
	writer.WriteInt32( 1, m_eResult );
	return writer.GetBuffer();
}

std::string CMsgGCClient::SerializeAsString() const
{
	CProtoBufLiteWriter writer;
	writer.WriteVarint( 1, m_unAppID );
	writer.WriteVarint( 2, m_unMsgType );
	writer.WriteBytes( 3, m_payload );
	if ( m_ulSteamID )
		writer.WriteFixed64( 4, m_ulSteamID );
	return writer.GetBuffer();
}

bool CMsgGCClient::ParseFromString( std::string_view data )
{
	*this = CMsgGCClient();
	m_unAppID = 0;
	CProtoBufLiteReader reader( data );
	ProtoBufLiteField_t field;
	while ( reader.BNextField( &field ) )
	{
		switch ( field.m_unField )
		{
		case 1: m_unAppID = uint32( field.m_ulValue ); break;
		case 2: m_unMsgType = uint32( field.m_ulValue ); break;
		case 3: m_payload = std::string( field.m_bytes ); break;
		case 4: m_ulSteamID = field.m_ulValue; break;
		default: break;
		}
	}
	return reader.BIsValid();
}

std::string CMsgMulti::SerializeAsString() const
{
	CProtoBufLiteWriter writer;
	if ( m_unSizeUnzipped )
		writer.WriteVarint( 1, m_unSizeUnzipped );
	writer.WriteBytes( 2, m_messageBody );
	return writer.GetBuffer();
}

bool CMsgMulti::ParseFromString( std::string_view data )
{
	*this = CMsgMulti();
	CProtoBufLiteReader reader( data );
	ProtoBufLiteField_t field;
	while ( reader.BNextField( &field ) )
	{
		if ( field.m_unField == 1 )
			m_unSizeUnzipped = uint32( field.m_ulValue );
		else if ( field.m_unField == 2 )
			m_messageBody = std::string( field.m_bytes );
	}
	return reader.BIsValid();
}

//-----------------------------------------------------------------------------
// GC packets
//-----------------------------------------------------------------------------
bool BStampGCPacketSteamID( std::string *pPacket, CSteamID steamID )
{
	const uint32 k_unGCProtoBufFlag = 0x80000000u;
	CByteReader reader( *pPacket );
	const uint32 unMsgType = reader.ReadUint32();
	const uint32 cubHdr = reader.ReadUint32();
	if ( !reader.BIsValid() || !( unMsgType & k_unGCProtoBufFlag ) || pPacket->size() - 8 < cubHdr )
		return false;

	CProtoBufLiteWriter field;
	field.WriteFixed64( 1, steamID.ConvertToUint64() );

	std::string stamped;
	stamped.reserve( pPacket->size() + field.GetBuffer().size() );
	AppendUint32( &stamped, unMsgType );
	AppendUint32( &stamped, cubHdr + uint32( field.GetBuffer().size() ) );
	stamped.append( *pPacket, 8, cubHdr );
	stamped += field.GetBuffer();
	stamped.append( *pPacket, 8 + cubHdr, std::string::npos );
	*pPacket = std::move( stamped );
	return true;
}

//-----------------------------------------------------------------------------
// Multi
//-----------------------------------------------------------------------------
std::string PackMultiMessage( const std::vector< std::string > &vecPayloads, size_t cubCompressAt )
{
	std::string bundle;
	for ( const std::string &payload : vecPayloads )
	{
		AppendUint32( &bundle, uint32( payload.size() ) );
		bundle += payload;
	}

	CMsgMulti msg;
	std::string compressed;
	if ( bundle.size() >= cubCompressAt && BGzip( bundle, &compressed ) )
	{
		msg.m_unSizeUnzipped = uint32( bundle.size() );
		msg.m_messageBody = std::move( compressed );
	}
	else
	{
		msg.m_messageBody = std::move( bundle );
	}
	return msg.SerializeAsString();
}

bool BUnpackMultiMessage( std::string_view multiBody, std::vector< std::string > *pvecPayloads )
{
	CMsgMulti msg;
	if ( !msg.ParseFromString( multiBody ) )
		return false;

	std::string bundle;
	if ( msg.m_unSizeUnzipped )
	{
		if ( !BGunzip( msg.m_messageBody, msg.m_unSizeUnzipped, &bundle ) )
			return false;
	}
	else
	{
		bundle = std::move( msg.m_messageBody );
	}

	pvecPayloads->clear();
	CByteReader reader( bundle );
	while ( !reader.BAtEnd() )
	{
		const uint32 cubPayload = reader.ReadUint32();
		std::string_view payload = reader.ReadBytes( cubPayload );
		if ( !reader.BIsValid() )
			return false;
		pvecPayloads->emplace_back( payload );
	}
	return true;
}
