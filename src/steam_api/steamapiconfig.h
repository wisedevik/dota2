//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: steam_api settings from the environment.
//
//============================================================================//

#ifndef STEAMAPICONFIG_H
#define STEAMAPICONFIG_H
#pragma once

#include <string>

#include "steam/steamclientpublic.h"

std::string GetSteamAPIConfigString( const char *pchName, const char *pchDefault );
std::string GetLocalAccountName();

// Same derivation the CM uses at logon.
CSteamID GetLocalSteamID();

#endif // STEAMAPICONFIG_H
