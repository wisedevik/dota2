//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: CCrypto on CommonCrypto and Security.framework.
//
//============================================================================//

#include "cmcrypto.h"

#include <CommonCrypto/CommonCrypto.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <zlib.h>

#include <cstring>

namespace
{

const size_t k_cubAESBlock = kCCBlockSizeAES128;	// 16
const size_t k_cubHMACIVPart = 13;					// HMAC part of the IV
const size_t k_cubRandomIVPart = 3;					// random part of the IV
const size_t k_cubHMACSecret = 16;					// first 16 bytes of the key

//-----------------------------------------------------------------------------
// Purpose: Owns one CoreFoundation reference.
//-----------------------------------------------------------------------------
template < typename T >
class CCFRef
{
public:
	CCFRef() : m_ref( nullptr ) {}
	explicit CCFRef( T ref ) : m_ref( ref ) {}
	CCFRef( const CCFRef & ) = delete;
	CCFRef &operator=( const CCFRef & ) = delete;
	~CCFRef()
	{
		if ( m_ref )
			CFRelease( m_ref );
	}

	T Get() const { return m_ref; }
	explicit operator bool() const { return m_ref != nullptr; }

private:
	T m_ref;
};

CFDataRef CreateCFData( std::string_view data )
{
	return CFDataCreate( nullptr, reinterpret_cast< const UInt8 * >( data.data() ), CFIndex( data.size() ) );
}

std::string CFDataToString( CFDataRef data )
{
	return std::string( reinterpret_cast< const char * >( CFDataGetBytePtr( data ) ), size_t( CFDataGetLength( data ) ) );
}

SecKeyRef ImportRSAKey( std::string_view der, bool bPrivate )
{
	CCFRef< CFMutableDictionaryRef > attrs( CFDictionaryCreateMutable( nullptr, 0,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks ) );
	CFDictionarySetValue( attrs.Get(), kSecAttrKeyType, kSecAttrKeyTypeRSA );
	CFDictionarySetValue( attrs.Get(), kSecAttrKeyClass, bPrivate ? kSecAttrKeyClassPrivate : kSecAttrKeyClassPublic );
	CCFRef< CFDataRef > data( CreateCFData( der ) );
	return SecKeyCreateWithData( data.Get(), attrs.Get(), nullptr );
}

std::string HMACSHA1( std::string_view key, std::string_view a, std::string_view b )
{
	CCHmacContext ctx;
	CCHmacInit( &ctx, kCCHmacAlgSHA1, key.data(), key.size() );
	CCHmacUpdate( &ctx, a.data(), a.size() );
	CCHmacUpdate( &ctx, b.data(), b.size() );
	std::string mac( CC_SHA1_DIGEST_LENGTH, '\0' );
	CCHmacFinal( &ctx, mac.data() );
	return mac;
}

bool BAES( CCOperation eOperation, CCOptions options, std::string_view key, const void *pIV,
	std::string_view in, std::string *pOut )
{
	pOut->assign( in.size() + k_cubAESBlock, '\0' );
	size_t cubMoved = 0;
	if ( CCCrypt( eOperation, kCCAlgorithmAES, options, key.data(), key.size(), pIV, in.data(), in.size(),
			pOut->data(), pOut->size(), &cubMoved ) != kCCSuccess )
		return false;
	pOut->resize( cubMoved );
	return true;
}

bool BCopyKeyDER( SecKeyRef key, std::string *pDER )
{
	CCFRef< CFDataRef > der( SecKeyCopyExternalRepresentation( key, nullptr ) );
	if ( !der )
		return false;
	*pDER = CFDataToString( der.Get() );
	return true;
}

bool BRSA( std::string_view keyDER, bool bPrivate, std::string_view in, std::string *pOut )
{
	CCFRef< SecKeyRef > key( ImportRSAKey( keyDER, bPrivate ) );
	if ( !key )
		return false;
	CCFRef< CFDataRef > data( CreateCFData( in ) );
	CCFRef< CFDataRef > out( bPrivate
		? SecKeyCreateDecryptedData( key.Get(), kSecKeyAlgorithmRSAEncryptionOAEPSHA1, data.Get(), nullptr )
		: SecKeyCreateEncryptedData( key.Get(), kSecKeyAlgorithmRSAEncryptionOAEPSHA1, data.Get(), nullptr ) );
	if ( !out )
		return false;
	*pOut = CFDataToString( out.Get() );
	return true;
}

} // namespace

std::string CCrypto::GenerateRandomBlock( size_t cubBlock )
{
	std::string block( cubBlock, '\0' );
	if ( SecRandomCopyBytes( kSecRandomDefault, cubBlock, block.data() ) != errSecSuccess )
		arc4random_buf( block.data(), cubBlock );
	return block;
}

uint32 CCrypto::CRC32( std::string_view data )
{
	return uint32( crc32( 0L, reinterpret_cast< const Bytef * >( data.data() ), uInt( data.size() ) ) );
}

std::string CCrypto::SymmetricEncryptWithHMACIV( std::string_view plain, std::string_view key )
{
	const std::string random = GenerateRandomBlock( k_cubRandomIVPart );
	const std::string mac = HMACSHA1( key.substr( 0, k_cubHMACSecret ), random, plain );
	const std::string iv = mac.substr( 0, k_cubHMACIVPart ) + random;

	std::string head, body;
	if ( !BAES( kCCEncrypt, kCCOptionECBMode, key, nullptr, iv, &head ) || head.size() != k_cubAESBlock )
		return {};
	if ( !BAES( kCCEncrypt, kCCOptionPKCS7Padding, key, iv.data(), plain, &body ) )
		return {};
	return head + body;
}

bool CCrypto::BSymmetricDecryptWithHMACIV( std::string_view cipher, std::string_view key, std::string *pPlain )
{
	if ( cipher.size() < 2 * k_cubAESBlock || cipher.size() % k_cubAESBlock )
		return false;
	std::string iv;
	if ( !BAES( kCCDecrypt, kCCOptionECBMode, key, nullptr, cipher.substr( 0, k_cubAESBlock ), &iv ) ||
		iv.size() != k_cubAESBlock )
		return false;
	if ( !BAES( kCCDecrypt, kCCOptionPKCS7Padding, key, iv.data(), cipher.substr( k_cubAESBlock ), pPlain ) )
		return false;

	const std::string mac = HMACSHA1( key.substr( 0, k_cubHMACSecret ), std::string_view( iv ).substr( k_cubHMACIVPart ), *pPlain );
	return memcmp( mac.data(), iv.data(), k_cubHMACIVPart ) == 0;
}

bool CCrypto::BGenerateRSAKeyPair( int cBits, std::string *pPrivateKey, std::string *pPublicKey )
{
	CCFRef< CFMutableDictionaryRef > params( CFDictionaryCreateMutable( nullptr, 0,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks ) );
	CFDictionarySetValue( params.Get(), kSecAttrKeyType, kSecAttrKeyTypeRSA );
	CCFRef< CFNumberRef > bits( CFNumberCreate( nullptr, kCFNumberIntType, &cBits ) );
	CFDictionarySetValue( params.Get(), kSecAttrKeySizeInBits, bits.Get() );

	CCFRef< SecKeyRef > privateKey( SecKeyCreateRandomKey( params.Get(), nullptr ) );
	if ( !privateKey )
		return false;
	CCFRef< SecKeyRef > publicKey( SecKeyCopyPublicKey( privateKey.Get() ) );
	return publicKey && BCopyKeyDER( privateKey.Get(), pPrivateKey ) && BCopyKeyDER( publicKey.Get(), pPublicKey );
}

bool CCrypto::BGetRSAPublicKey( std::string_view privateKey, std::string *pPublicKey )
{
	CCFRef< SecKeyRef > key( ImportRSAKey( privateKey, true ) );
	if ( !key )
		return false;
	CCFRef< SecKeyRef > publicKey( SecKeyCopyPublicKey( key.Get() ) );
	return publicKey && BCopyKeyDER( publicKey.Get(), pPublicKey );
}

bool CCrypto::BRSAEncrypt( std::string_view publicKey, std::string_view plain, std::string *pCipher )
{
	return BRSA( publicKey, false, plain, pCipher );
}

bool CCrypto::BRSADecrypt( std::string_view privateKey, std::string_view cipher, std::string *pPlain )
{
	return BRSA( privateKey, true, cipher, pPlain );
}
