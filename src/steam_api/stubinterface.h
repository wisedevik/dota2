//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Vtable-backed object handed to the game as a Steamworks interface.
//
//============================================================================//

#ifndef STUBINTERFACE_H
#define STUBINTERFACE_H
#pragma once

#include "tier0/platform.h"

extern "C" uint64 SteamStubReturnZero();

class CStubInterface
{
public:
	CStubInterface()
	{
		for ( int i = 0; i < k_cVTableSlots; ++i )
			m_rgpfnVTable[i] = reinterpret_cast< void * >( &SteamStubReturnZero );
		m_object.m_ppfnVTable = m_rgpfnVTable;
	}

	CStubInterface( const CStubInterface & ) = delete;
	CStubInterface &operator=( const CStubInterface & ) = delete;

	template < typename Fn >
	void SetMethod( int iSlot, Fn pfn )
	{
		if ( iSlot >= 0 && iSlot < k_cVTableSlots )
			m_rgpfnVTable[iSlot] = reinterpret_cast< void * >( pfn );
	}

	// What the game gets from CreateInterface / GetISteam*.
	void *GetInterface() { return &m_object; }

private:
	static const int k_cVTableSlots = 160;

	struct StubObject_t
	{
		void **m_ppfnVTable;
		char m_rgubPadding[256];	// in case the engine reads instance fields
	};

	void *m_rgpfnVTable[k_cVTableSlots];
	StubObject_t m_object;
};

#endif // STUBINTERFACE_H
