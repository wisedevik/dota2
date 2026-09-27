//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Interface a CM delivers GC traffic to.
//
//============================================================================//

#ifndef IGCMESSAGEROUTER_H
#define IGCMESSAGEROUTER_H
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "steam/steamclientpublic.h"

struct GCPacket_t
{
	uint32 m_unMsgType;
	std::string m_packet;
};

class IGCMessageRouter
{
public:
	virtual ~IGCMessageRouter() {}

	// Called from connection threads.
	virtual void RouteMessageToGC( CSteamID steamID, uint32 unAppID, uint32 unMsgType, std::string_view packet,
		std::vector< GCPacket_t > *pvecReplies ) = 0;
};

#endif // IGCMESSAGEROUTER_H
