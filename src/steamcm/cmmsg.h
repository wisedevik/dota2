//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Steam CM messages and their encoding.
//
//============================================================================//

#ifndef CMMSG_H
#define CMMSG_H
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "steam/steamclientpublic.h"
#include "tier0/platform.h"

enum EMsg
{
	k_EMsgInvalid = 0,
	k_EMsgMulti = 1,
	k_EMsgClientHeartBeat = 703,
	k_EMsgClientLogOff = 706,
	k_EMsgClientLogOnResponse = 751,
	k_EMsgClientLoggedOff = 757,
	k_EMsgChannelEncryptRequest = 1303,
	k_EMsgChannelEncryptResponse = 1304,
	k_EMsgChannelEncryptResult = 1305,
	k_EMsgClientToGC = 5452,
	k_EMsgClientFromGC = 5453,
	k_EMsgClientLogon = 5514,
};

const uint32 k_unEMsgProtoBufFlag = 0x80000000u;
const uint32 k_unCMPacketMagic = 0x31305456;				// "VT01"
const uint32 k_cubCMPacketMax = 16u * 1024 * 1024;
const uint32 k_unChannelEncryptProtocol = 1;
const uint32 k_unClientLogonProtocol = 65580;				// SteamKit MsgClientLogon.CurrentProtocol
const uint16 k_usCMDefaultPort = 27017;					// one of Steam's CM TCP ports
const uint32 k_unDotaAppID = 570;
const size_t k_cubMultiCompressThreshold = 1024;

// No passwords: the account id is derived from the name.
uint32 GetAccountIDForName( std::string_view accountName );
CSteamID GetSteamIDForAccountName( std::string_view accountName );

// Steam's CMsgProtoBufHeader (not the GC one of the same name).
struct MsgHdrProtoBuf_t
{
	uint64 m_ulSteamID = 0;					// 1
	int32 m_nSessionID = 0;					// 2
	uint32 m_unRoutingAppID = 0;			// 3
	JobID_t m_jobIDSource = k_GIDNil;		// 10
	JobID_t m_jobIDTarget = k_GIDNil;		// 11
	EResult m_eResult = k_EResultFail;		// 13, sent only when m_bHasResult
	bool m_bHasResult = false;
};

class CCMPacket
{
public:
	static bool BParse( std::string_view payload, CCMPacket *pPacket );
	static std::string SerializeProtoBuf( EMsg eMsg, const MsgHdrProtoBuf_t &hdr, std::string_view body );
	static std::string SerializeSimple( EMsg eMsg, std::string_view body );

	EMsg GetEMsg() const { return m_eMsg; }
	bool BIsProtoBuf() const { return m_bProtoBuf; }
	const MsgHdrProtoBuf_t &GetProtoHdr() const { return m_hdr; }
	const std::string &GetBody() const { return m_body; }

private:
	EMsg m_eMsg = k_EMsgInvalid;
	bool m_bProtoBuf = false;
	MsgHdrProtoBuf_t m_hdr;		// protobuf messages only
	std::string m_body;
};

// Message bodies, laid out as in SteamKit.
struct MsgChannelEncryptRequest_t
{
	uint32 m_unProtocolVersion = k_unChannelEncryptProtocol;
	EUniverse m_eUniverse = k_EUniversePublic;
	std::string m_challenge;		// 16 random bytes, echoed back RSA-encrypted

	std::string SerializeAsString() const;
	bool ParseFromString( std::string_view data );
};

struct MsgChannelEncryptResponse_t
{
	uint32 m_unProtocolVersion = k_unChannelEncryptProtocol;
	std::string m_encryptedKey;		// RSA( session key || challenge ), key size bytes

	std::string SerializeAsString() const;		// appends CRC32( m_encryptedKey ) and a zero word
	bool ParseFromString( std::string_view data );
};

struct MsgChannelEncryptResult_t
{
	EResult m_eResult = k_EResultOK;

	std::string SerializeAsString() const;
	bool ParseFromString( std::string_view data );
};

struct CMsgClientLogon
{
	uint32 m_unProtocolVersion = k_unClientLogonProtocol;	// 1
	std::string m_strAccountName;							// 50

	std::string SerializeAsString() const;
	bool ParseFromString( std::string_view data );
};

struct CMsgClientLogonResponse
{
	EResult m_eResult = k_EResultFail;				// 1
	int32 m_nOutOfGameHeartbeatSeconds = 9;			// 2
	int32 m_nInGameHeartbeatSeconds = 9;			// 3
	uint32 m_rtime32ServerTime = 0;					// 5
	uint32 m_unCellID = 0;							// 7

	std::string SerializeAsString() const;
	bool ParseFromString( std::string_view data );
};

struct CMsgClientLoggedOff
{
	EResult m_eResult = k_EResultOK;	// 1

	std::string SerializeAsString() const;
};

// Body of ClientToGC / ClientFromGC.
struct CMsgGCClient
{
	uint32 m_unAppID = k_unDotaAppID;	// 1
	uint32 m_unMsgType = 0;				// 2, GC message type incl. the GC protobuf flag
	std::string m_payload;				// 3, the whole GC packet
	uint64 m_ulSteamID = 0;				// 4

	std::string SerializeAsString() const;
	bool ParseFromString( std::string_view data );
};

// EMsg Multi: [uint32 length][payload]..., gzipped when m_unSizeUnzipped != 0.
struct CMsgMulti
{
	uint32 m_unSizeUnzipped = 0;	// 1, 0 = not compressed
	std::string m_messageBody;		// 2

	std::string SerializeAsString() const;
	bool ParseFromString( std::string_view data );
};

// Appends client_steam_id to the GC header, like Steam does.
bool BStampGCPacketSteamID( std::string *pPacket, CSteamID steamID );

std::string PackMultiMessage( const std::vector< std::string > &vecPayloads,
	size_t cubCompressAt = k_cubMultiCompressThreshold );
bool BUnpackMultiMessage( std::string_view multiBody, std::vector< std::string > *pvecPayloads );

#endif // CMMSG_H
