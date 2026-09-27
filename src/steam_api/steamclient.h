//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: ISteamClient and the interfaces it hands out.
//
//============================================================================//

#ifndef STEAMCLIENT_H
#define STEAMCLIENT_H
#pragma once

void *GetSteamClientInterface();	// ISteamClient017
void *GetSteamUserInterface();		// ISteamUser019
void *GetGenericStubInterface();	// every slot returns 0

#endif // STEAMCLIENT_H
