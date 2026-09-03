#pragma once

// Streamline's production loader verifies the NVIDIA signature on every
// module it loads. MinGW-w64's wintrust.h currently omits the Windows 8+
// multiple-signature fields used by sl_security.h, even though the API is
// available on every Windows version supported by PlutoGE.
#if defined(__MINGW32__)
#include <cstddef>
#include <wintrust.h>

struct PlutoStreamlineSignatureSettings
{
    DWORD cbStruct;
    DWORD dwIndex;
    DWORD dwFlags;
    DWORD cSecondarySigs;
    DWORD dwVerifiedSigIndex;
    PCERT_STRONG_SIGN_PARA pCryptoPolicy;
};

struct PlutoStreamlineWinTrustData
{
    DWORD cbStruct;
    LPVOID pPolicyCallbackData;
    LPVOID pSIPClientData;
    DWORD dwUIChoice;
    DWORD fdwRevocationChecks;
    DWORD dwUnionChoice;
    union
    {
        WINTRUST_FILE_INFO *pFile;
        WINTRUST_CATALOG_INFO *pCatalog;
        WINTRUST_BLOB_INFO *pBlob;
        WINTRUST_SGNR_INFO *pSgnr;
        WINTRUST_CERT_INFO *pCert;
    };
    DWORD dwStateAction;
    HANDLE hWVTStateData;
    WCHAR *pwszURLReference;
    DWORD dwProvFlags;
    DWORD dwUIContext;
    PlutoStreamlineSignatureSettings *pSignatureSettings;
};

static_assert(offsetof(PlutoStreamlineWinTrustData, pSignatureSettings) == sizeof(WINTRUST_DATA),
              "The MinGW WinTrust compatibility structure must preserve the Windows ABI");

#define WINTRUST_SIGNATURE_SETTINGS PlutoStreamlineSignatureSettings
#define WINTRUST_DATA PlutoStreamlineWinTrustData
#define WSS_VERIFY_SPECIFIC 0x00000001
#define WSS_GET_SECONDARY_SIG_COUNT 0x00000002
#include <sl_security.h>
#undef WSS_GET_SECONDARY_SIG_COUNT
#undef WSS_VERIFY_SPECIFIC
#undef WINTRUST_DATA
#undef WINTRUST_SIGNATURE_SETTINGS
#else
#include <sl_security.h>
#endif
