//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: ISteamGameCoordinator (SteamGameCoordinator001), ABI-compatible with the SDK.
//
//============================================================================//

#ifndef ISTEAMGAMECOORDINATOR_H
#define ISTEAMGAMECOORDINATOR_H
#pragma once

#include "tier0/platform.h"

#define STEAMGAMECOORDINATOR_INTERFACE_VERSION "SteamGameCoordinator001"

// Callback id of GCMessageAvailable_t: k_iSteamGameCoordinatorCallbacks + 1.
const int k_iGCMessageAvailableCallback = 1701;

enum EGCResults
{
	k_EGCResultOK = 0,
	k_EGCResultNoMessage = 1,		// no message available for RetrieveMessage
	k_EGCResultBufferTooSmall = 2,	// the caller's buffer was too small
	k_EGCResultNotLoggedOn = 3,
	k_EGCResultInvalidMessage = 4,
};

class ISteamGameCoordinator
{
public:
	// pubData is the whole GC packet: [type][header length][header][body].
	virtual EGCResults SendMessage( uint32 unMsgType, const void *pubData, uint32 cubData ) = 0;

	virtual bool IsMessageAvailable( uint32 *pcubMsgSize ) = 0;

	virtual EGCResults RetrieveMessage( uint32 *punMsgType, void *pubDest, uint32 cubDest,
		uint32 *pcubMsgSize ) = 0;
};

// GCMessageAvailable_t
struct GCMessageAvailable_t
{
	uint32 m_nMessageSize;
};

#endif // ISTEAMGAMECOORDINATOR_H
