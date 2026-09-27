//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Minimal protobuf wire format reader/writer for the CM messages.
//
//============================================================================//

#ifndef PROTOBUFLITE_H
#define PROTOBUFLITE_H
#pragma once

#include <string>
#include <string_view>

#include "tier0/platform.h"

enum EProtoBufWireType
{
	k_EProtoBufWireTypeVarint = 0,
	k_EProtoBufWireTypeFixed64 = 1,
	k_EProtoBufWireTypeLengthDelimited = 2,
	k_EProtoBufWireTypeFixed32 = 5,
};

//-----------------------------------------------------------------------------
// Purpose: Appends fields to a message buffer.
//-----------------------------------------------------------------------------
class CProtoBufLiteWriter
{
public:
	void WriteVarint( uint32 unField, uint64 ulValue )
	{
		WriteTag( unField, k_EProtoBufWireTypeVarint );
		WriteRawVarint( ulValue );
	}

	// int32 is sign-extended to 64 bits on the wire, as protobuf does.
	void WriteInt32( uint32 unField, int32 nValue ) { WriteVarint( unField, uint64( int64( nValue ) ) ); }

	void WriteFixed64( uint32 unField, uint64 ulValue )
	{
		WriteTag( unField, k_EProtoBufWireTypeFixed64 );
		for ( int i = 0; i < 8; ++i )
			m_buf.push_back( char( ( ulValue >> ( 8 * i ) ) & 0xff ) );
	}

	void WriteBytes( uint32 unField, std::string_view data )
	{
		WriteTag( unField, k_EProtoBufWireTypeLengthDelimited );
		WriteRawVarint( data.size() );
		m_buf.append( data );
	}

	const std::string &GetBuffer() const { return m_buf; }

private:
	void WriteTag( uint32 unField, EProtoBufWireType eType ) { WriteRawVarint( ( uint64( unField ) << 3 ) | eType ); }

	void WriteRawVarint( uint64 ulValue )
	{
		while ( ulValue >= 0x80 )
		{
			m_buf.push_back( char( ( ulValue & 0x7f ) | 0x80 ) );
			ulValue >>= 7;
		}
		m_buf.push_back( char( ulValue ) );
	}

	std::string m_buf;
};

//-----------------------------------------------------------------------------
// Purpose: One decoded field.
//-----------------------------------------------------------------------------
struct ProtoBufLiteField_t
{
	uint32 m_unField = 0;
	EProtoBufWireType m_eType = k_EProtoBufWireTypeVarint;
	uint64 m_ulValue = 0;			// varint, fixed32, fixed64
	std::string_view m_bytes;		// length-delimited
};

//-----------------------------------------------------------------------------
// Purpose: Walks the fields of one message. BNextField() is false at the end
//          or on malformed input; BIsValid() tells the two apart.
//-----------------------------------------------------------------------------
class CProtoBufLiteReader
{
public:
	explicit CProtoBufLiteReader( std::string_view data ) : m_data( data ), m_nPos( 0 ), m_bValid( true ) {}

	bool BNextField( ProtoBufLiteField_t *pField )
	{
		if ( m_nPos >= m_data.size() )
			return false;

		uint64 ulTag;
		if ( !BReadRawVarint( &ulTag ) )
			return BFail();
		pField->m_unField = uint32( ulTag >> 3 );
		pField->m_eType = EProtoBufWireType( ulTag & 7 );
		pField->m_bytes = {};

		switch ( pField->m_eType )
		{
		case k_EProtoBufWireTypeVarint:
			return BReadRawVarint( &pField->m_ulValue ) || BFail();

		case k_EProtoBufWireTypeFixed64:
		case k_EProtoBufWireTypeFixed32:
		{
			const size_t cubValue = pField->m_eType == k_EProtoBufWireTypeFixed64 ? 8 : 4;
			if ( m_data.size() - m_nPos < cubValue )
				return BFail();
			pField->m_ulValue = 0;
			for ( size_t i = 0; i < cubValue; ++i )
				pField->m_ulValue |= uint64( uint8( m_data[m_nPos + i] ) ) << ( 8 * i );
			m_nPos += cubValue;
			return true;
		}

		case k_EProtoBufWireTypeLengthDelimited:
		{
			uint64 ulLength;
			if ( !BReadRawVarint( &ulLength ) || m_data.size() - m_nPos < ulLength )
				return BFail();
			pField->m_bytes = m_data.substr( m_nPos, size_t( ulLength ) );
			m_nPos += size_t( ulLength );
			return true;
		}

		default:
			return BFail();
		}
	}

	bool BIsValid() const { return m_bValid; }

private:
	bool BReadRawVarint( uint64 *pulValue )
	{
		*pulValue = 0;
		for ( int nShift = 0; nShift < 64 && m_nPos < m_data.size(); nShift += 7 )
		{
			const uint8 ubByte = uint8( m_data[m_nPos++] );
			*pulValue |= uint64( ubByte & 0x7f ) << nShift;
			if ( !( ubByte & 0x80 ) )
				return true;
		}
		return false;
	}

	bool BFail()
	{
		m_bValid = false;
		return false;
	}

	std::string_view m_data;
	size_t m_nPos;
	bool m_bValid;
};

#endif // PROTOBUFLITE_H
