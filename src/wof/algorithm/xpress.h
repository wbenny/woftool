#pragma once
#include <ntdll.h>


//////////////////////////////////////////////////////////////////////////
// Private functions.
//////////////////////////////////////////////////////////////////////////

NTSTATUS
NTAPI
WofpAlgorithmXpressInitialize(
    _Outptr_ PVOID* AlgorithmContext
    );

VOID
NTAPI
WofpAlgorithmXpressDestroy(
    _In_ PVOID AlgorithmContext
    );

NTSTATUS
NTAPI
WofpAlgorithmXpressCompress(
    _In_ PVOID AlgorithmContext,
    _In_ PVOID UncompressedChunkBuffer,
    _In_ ULONG UncompressedChunkBufferSize,
    _In_ PVOID CompressedChunkBuffer,
    _In_ ULONG CompressedChunkBufferSize,
    _In_ PULONG FinalCompressedSize
    );
