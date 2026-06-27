/*
===========================================================================
STRAFE 64 license verification.

Verifies Ed25519-signed license keys minted by tools/keygen/keygen.py. The
engine ships only the public key (license_pubkey.h), so it can check that a
key is genuine but can never mint one. Verification is fully offline -- no
server, no network.

The wire format MUST match tools/keygen/license_format.py byte-for-byte:

    payload (12 bytes, big-endian)
        0  version (==LICENSE_FORMAT_VERSION)
        1  product (==LICENSE_PRODUCT_ID)
        2  tier
        3  flags
        4  issue days since 2020-01-01 (uint16)
        6  serial (48-bit)
    signed blob = signature(64) || payload(12)         = 76 bytes
    key string  = "S64-" + crockford_base32(blob), dash grouped
===========================================================================
*/

#include "q_shared.h"
#include "qcommon.h"
#include "tweetnacl.h"
#include "license_pubkey.h"

#define LICENSE_FORMAT_VERSION	1
#define LICENSE_PRODUCT_ID		0x64
#define LICENSE_TIER_DEMO		0		/* free web/demo build entitlement */
#define LICENSE_PAYLOAD_LEN		12
#define LICENSE_SIG_LEN			64
#define LICENSE_BLOB_LEN		(LICENSE_SIG_LEN + LICENSE_PAYLOAD_LEN)	/* 76 */

cvar_t	*com_licensed;		/* ROM 0/1: is a valid key installed?         */
cvar_t	*com_licenseTier;	/* ROM int: tier byte of the installed key    */
static cvar_t	*cl_licenseKey;	/* ARCHIVE: the stored key string (persisted) */

typedef struct {
	int		tier;
	int		flags;
	int		issueDays;
	double	serial;			/* 48-bit, doesn't fit a portable int cleanly */
} licenseInfo_t;

/*
================
randombytes

TweetNaCl declares this extern for its key-generation and signing paths,
which the engine never calls (it only verifies). Provide a definition so the
link resolves; trip an error if anything ever actually reaches it.
================
*/
void randombytes( unsigned char *x, unsigned long long n ) {
	(void)x; (void)n;
	Com_Error( ERR_FATAL, "randombytes called: the engine must never sign, only verify" );
}

/*
================
Lic_Base32Value

Crockford base32 digit -> value, or -1 for a non-digit. Case-insensitive and
tolerant of the usual hand-transcription look-alikes (O->0, I/L->1). Mirrors
the decode table in license_format.py.
================
*/
static int Lic_Base32Value( int c ) {
	if ( c >= 'a' && c <= 'z' ) c -= 'a' - 'A';	/* upper-case */

	if ( c >= '0' && c <= '9' ) return c - '0';
	if ( c == 'O' ) return 0;
	if ( c == 'I' || c == 'L' ) return 1;
	/* letters A..Z minus I,L,O,U map to values 10..31 */
	switch ( c ) {
		case 'A': return 10; case 'B': return 11; case 'C': return 12;
		case 'D': return 13; case 'E': return 14; case 'F': return 15;
		case 'G': return 16; case 'H': return 17; case 'J': return 18;
		case 'K': return 19; case 'M': return 20; case 'N': return 21;
		case 'P': return 22; case 'Q': return 23; case 'R': return 24;
		case 'S': return 25; case 'T': return 26; case 'V': return 27;
		case 'W': return 28; case 'X': return 29; case 'Y': return 30;
		case 'Z': return 31;
		default:  return -1;
	}
}

/*
================
Lic_DecodeKey

Strips the "S64" prefix and any separators, then base32-decodes the body into
the 76-byte signed blob. Returns qtrue on a well-formed key (signature not yet
checked). Uses a big-endian multiply-add (out = out*32 + digit) so the byte
alignment matches the Python encoder exactly, including its high pad bits.
================
*/
static qboolean Lic_DecodeKey( const char *key, byte blob[LICENSE_BLOB_LEN] ) {
	int		digits[ ( LICENSE_BLOB_LEN * 8 + 4 ) / 5 ];	/* 122 base32 digits */
	int		ndigits = 0;
	int		i, j;
	const char	*p = key;

	if ( !key ) {
		return qfalse;
	}

	/* skip an optional leading "S64" / "s64" product prefix */
	if ( ( p[0] == 'S' || p[0] == 's' ) && p[1] == '6' && p[2] == '4' ) {
		p += 3;
	}

	/* collect base32 digits, ignoring dashes / spaces / other separators */
	for ( ; *p; p++ ) {
		int v;
		if ( *p == '-' || *p == ' ' || *p == '\t' || *p == '_' ) {
			continue;
		}
		v = Lic_Base32Value( (unsigned char)*p );
		if ( v < 0 ) {
			return qfalse;			/* illegal character */
		}
		if ( ndigits >= (int)ARRAY_LEN( digits ) ) {
			return qfalse;			/* too long */
		}
		digits[ ndigits++ ] = v;
	}

	if ( ndigits != (int)ARRAY_LEN( digits ) ) {
		return qfalse;				/* wrong length */
	}

	/* blob = base32 number, big-endian, via repeated *32 + digit */
	Com_Memset( blob, 0, LICENSE_BLOB_LEN );
	for ( i = 0; i < ndigits; i++ ) {
		int carry = digits[i];
		for ( j = LICENSE_BLOB_LEN - 1; j >= 0; j-- ) {
			int t = blob[j] * 32 + carry;
			blob[j] = (byte)( t & 0xFF );
			carry = t >> 8;
		}
		if ( carry != 0 ) {
			return qfalse;			/* overflow: value exceeds 76 bytes (bad key) */
		}
	}
	return qtrue;
}

/*
================
Lic_VerifyKey

Full validation of a key string: decode, verify the Ed25519 signature against
the embedded public key, then sanity-check the recovered payload. On success
fills `info` (may be NULL) and returns qtrue.
================
*/
static qboolean Lic_VerifyKey( const char *key, licenseInfo_t *info ) {
	byte				blob[ LICENSE_BLOB_LEN ];
	byte				message[ LICENSE_BLOB_LEN ];	/* crypto_sign_open needs n bytes */
	unsigned long long	mlen = 0;
	int					i;

	if ( !Lic_DecodeKey( key, blob ) ) {
		return qfalse;
	}

	/* verify signature; recovers the payload into `message` on success */
	if ( crypto_sign_open( message, &mlen, blob, LICENSE_BLOB_LEN,
						   license_public_key ) != 0 ) {
		return qfalse;
	}
	if ( mlen != LICENSE_PAYLOAD_LEN ) {
		return qfalse;
	}

	/* recovered payload must be ours */
	if ( message[0] != LICENSE_FORMAT_VERSION || message[1] != LICENSE_PRODUCT_ID ) {
		return qfalse;
	}

	if ( info ) {
		info->tier      = message[2];
		info->flags     = message[3];
		info->issueDays = ( message[4] << 8 ) | message[5];
		info->serial    = 0;
		for ( i = 6; i < LICENSE_PAYLOAD_LEN; i++ ) {
			info->serial = info->serial * 256.0 + message[i];
		}
	}
	return qtrue;
}

/*
================
Com_LicenseValid

Public predicate used by the gameplay gate.
================
*/
qboolean Com_LicenseValid( void ) {
	return ( com_licensed && com_licensed->integer ) ? qtrue : qfalse;
}

/*
================
Com_ApplyLicense

Validate `key`; on success mark the engine licensed and record the tier.
================
*/
static qboolean Com_ApplyLicense( const char *key, qboolean verbose ) {
	licenseInfo_t info;

	if ( !key || !key[0] ) {
		return qfalse;
	}
	if ( !Lic_VerifyKey( key, &info ) ) {
		if ( verbose ) {
			Com_Printf( S_COLOR_RED "License key is not valid.\n" );
		}
		Cvar_Set( "com_licensed", "0" );
		Cvar_Set( "com_licenseTier", "0" );
		return qfalse;
	}

	Cvar_Set( "com_licensed", "1" );
	Cvar_SetValue( "com_licenseTier", info.tier );
	if ( verbose ) {
		Com_Printf( S_COLOR_GREEN "License accepted." S_COLOR_WHITE
					" tier %i, serial %013.0f\n", info.tier, info.serial );
	}
	return qtrue;
}

/*
================
Com_License_f

Console command:  license <KEY>
Installs and persists a key. With no argument, reports current status.
================
*/
static void Com_License_f( void ) {
	if ( Cmd_Argc() < 2 ) {
		if ( Com_LicenseValid() ) {
			Com_Printf( "STRAFE 64 is registered (tier %i).\n",
						com_licenseTier->integer );
		} else {
			Com_Printf( "STRAFE 64 is unregistered. Enter your key with:\n"
						"    license <YOUR-KEY>\n" );
		}
		return;
	}

	/* Cmd_Args() keeps the whole tail so a pasted, space-containing key works */
	if ( Com_ApplyLicense( Cmd_Args(), qtrue ) ) {
		Cvar_Set( "cl_licenseKey", Cmd_Args() );	/* CVAR_ARCHIVE -> persisted */
	}
}

#ifdef LICENSE_SELFTEST
/* Test hook (see tools/keygen/ctest): returns the tier byte, or -1 if the key
   is rejected. Compiled out of the real engine build. */
int Com_LicenseSelfTest( const char *key ) {
	licenseInfo_t info;
	if ( !Lic_VerifyKey( key, &info ) ) {
		return -1;
	}
	return info.tier;
}
#endif

/*
================
Com_InitLicense

Registers cvars/command and validates any previously stored key. Call once
during Com_Init, after the filesystem is up (so the archived config has been
read and cl_licenseKey is populated).
================
*/
void Com_InitLicense( void ) {
	com_licensed     = Cvar_Get( "com_licensed", "0", CVAR_ROM );
	com_licenseTier  = Cvar_Get( "com_licenseTier", "0", CVAR_ROM );
	cl_licenseKey    = Cvar_Get( "cl_licenseKey", "", CVAR_ARCHIVE | CVAR_PROTECTED );

	Cmd_AddCommand( "license", Com_License_f );

#ifdef STRAFE64_WEB_DEMO
	/* The browser build is a free, self-contained demo: it ships only the
	   curated demo maps, so the gameplay gate is satisfied automatically and
	   no key is required. The full game (every map and mode) still needs a
	   purchased key in the native build. */
	Cvar_Set( "com_licensed", "1" );
	Cvar_SetValue( "com_licenseTier", LICENSE_TIER_DEMO );
	Com_Printf( "License: " S_COLOR_GREEN "WEB DEMO" S_COLOR_WHITE
				" -- buy the full game for every map and mode.\n" );
	return;
#endif

	if ( Com_ApplyLicense( cl_licenseKey->string, qfalse ) ) {
		Com_Printf( "License: registered (tier %i).\n", com_licenseTier->integer );
	} else {
		Com_Printf( "License: " S_COLOR_YELLOW "UNREGISTERED" S_COLOR_WHITE
					" -- install a key with: license <YOUR-KEY>\n" );
	}
}
