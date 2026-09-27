//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Steam CM channel crypto: RSA-OAEP key exchange and AES with an HMAC IV.
//
//============================================================================//

#ifndef CMCRYPTO_H
#define CMCRYPTO_H
#pragma once

#include <string>
#include <string_view>

#include "tier0/platform.h"

const size_t k_cubSessionKey = 32;		// AES-256
const size_t k_cubChannelChallenge = 16;

class CCrypto
{
public:
	static std::string GenerateRandomBlock( size_t cubBlock );
	static uint32 CRC32( std::string_view data );

	static std::string SymmetricEncryptWithHMACIV( std::string_view plain, std::string_view key );
	// False when the frame is truncated, badly padded or fails the HMAC.
	static bool BSymmetricDecryptWithHMACIV( std::string_view cipher, std::string_view key,
		std::string *pPlain );

	// RSA keys travel as PKCS#1 DER (RSAPrivateKey / RSAPublicKey).
	static bool BGenerateRSAKeyPair( int cBits, std::string *pPrivateKey, std::string *pPublicKey );
	static bool BGetRSAPublicKey( std::string_view privateKey, std::string *pPublicKey );
	static bool BRSAEncrypt( std::string_view publicKey, std::string_view plain, std::string *pCipher );	// OAEP/SHA-1
	static bool BRSADecrypt( std::string_view privateKey, std::string_view cipher, std::string *pPlain );	// OAEP/SHA-1
};

#endif // CMCRYPTO_H
