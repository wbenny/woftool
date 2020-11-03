#include "algorithm.h"

#include "algorithm/xpress.h"
#if defined(WOF_ALGORITHM_ENABLE_LZX)
#include "algorithm/lzx.h"
#endif


//////////////////////////////////////////////////////////////////////////
// Variables.
//////////////////////////////////////////////////////////////////////////

WOF_ALGORITHM_VTABLE WofpXpressAlgorithmVTable = {
    .Initialize = &WofpAlgorithmXpressInitialize,
    .Destroy    = &WofpAlgorithmXpressDestroy,
    .Compress   = &WofpAlgorithmXpressCompress,
};

#if defined(WOF_ALGORITHM_ENABLE_LZX)
WOF_ALGORITHM_VTABLE WofpLzxAlgorithmVTable = {
    .Initialize = &WofpAlgorithmLzxInitialize,
    .Destroy    = &WofpAlgorithmLzxDestroy,
    .Compress   = &WofpAlgorithmLzxCompress,
};
#endif

WOF_ALGORITHM WofpAlgorithmTable[] = {
    [FILE_PROVIDER_COMPRESSION_XPRESS4K] = {
        .VTable     = &WofpXpressAlgorithmVTable,
        .Algorithm  = FILE_PROVIDER_COMPRESSION_XPRESS4K,
        .ChunkSize  = 4096,
    },

     [FILE_PROVIDER_COMPRESSION_XPRESS8K] = {
        .VTable     = &WofpXpressAlgorithmVTable,
        .Algorithm  = FILE_PROVIDER_COMPRESSION_XPRESS8K,
        .ChunkSize  = 8192
    },

     [FILE_PROVIDER_COMPRESSION_XPRESS16K] = {
        .VTable     = &WofpXpressAlgorithmVTable,
        .Algorithm  = FILE_PROVIDER_COMPRESSION_XPRESS16K,
        .ChunkSize  = 16384
    },

    [FILE_PROVIDER_COMPRESSION_LZX] = {
#if defined(WOF_ALGORITHM_ENABLE_LZX)
        .VTable     = &WofpLzxAlgorithmVTable,
#else
        .VTable     = NULL,
#endif
        .Algorithm  = FILE_PROVIDER_COMPRESSION_LZX,
        .ChunkSize  = 32768
    },
};

ULONG WofpAlgorithmTableElements = RTL_NUMBER_OF(WofpAlgorithmTable);

C_ASSERT(
    RTL_NUMBER_OF(WofpAlgorithmTable) == FILE_PROVIDER_COMPRESSION_MAXIMUM
);



//////////////////////////////////////////////////////////////////////////
// Private functions.
//////////////////////////////////////////////////////////////////////////

NTSTATUS
NTAPI
WofpFileProviderCompressionToAlgorithm(
    _In_ ULONG Algorithm,
    _Outptr_ PWOF_ALGORITHM* AlgorithmVTable
    )
{
    if (Algorithm >= FILE_PROVIDER_COMPRESSION_MAXIMUM)
    {
        return STATUS_UNSUPPORTED_COMPRESSION;
    }

    if (!WofpAlgorithmTable[Algorithm].VTable)
    {
        return STATUS_UNSUPPORTED_COMPRESSION;
    }

    *AlgorithmVTable = &WofpAlgorithmTable[Algorithm];

    return STATUS_SUCCESS;
}
