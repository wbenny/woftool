#pragma once
#include <ntdll.h>


//////////////////////////////////////////////////////////////////////////
// Definitions
//////////////////////////////////////////////////////////////////////////

// #define WOF_ALGORITHM_ENABLE_LZX

//////////////////////////////////////////////////////////////////////////
// Function type definitions.
//////////////////////////////////////////////////////////////////////////

typedef NTSTATUS (NTAPI* PWOF_ALGORITHM_METHOD_INITIALIZE)(
    _Outptr_ PVOID* CompressionContext
    );

typedef VOID (NTAPI* PWOF_ALGORITHM_METHOD_DESTROY)(
    _In_ PVOID CompressionContext
    );

typedef NTSTATUS (NTAPI* PWOF_ALGORITHM_METHOD_COMPRESS)(
    _In_ PVOID CompressionContext,
    _In_ PVOID UncompressedChunkBuffer,
    _In_ ULONG UncompressedChunkBufferSize,
    _In_ PVOID CompressedChunkBuffer,
    _In_ ULONG CompressedChunkBufferSize,
    _In_ PULONG FinalCompressedSize
    );


//////////////////////////////////////////////////////////////////////////
// Structures.
//////////////////////////////////////////////////////////////////////////

typedef struct _WOF_ALGORITHM_VTABLE
{
    PWOF_ALGORITHM_METHOD_INITIALIZE Initialize;
    PWOF_ALGORITHM_METHOD_DESTROY Destroy;
    PWOF_ALGORITHM_METHOD_COMPRESS Compress;
} WOF_ALGORITHM_VTABLE, *PWOF_ALGORITHM_VTABLE;

typedef struct _WOF_ALGORITHM
{
    PWOF_ALGORITHM_VTABLE VTable;
    ULONG Algorithm;
    ULONG ChunkSize;
} WOF_ALGORITHM, *PWOF_ALGORITHM;


//////////////////////////////////////////////////////////////////////////
// Variables.
//////////////////////////////////////////////////////////////////////////

extern WOF_ALGORITHM WofpAlgorithmTable[];
extern ULONG WofpAlgorithmTableElements;


//////////////////////////////////////////////////////////////////////////
// Private functions.
//////////////////////////////////////////////////////////////////////////

NTSTATUS
NTAPI
WofpFileProviderCompressionToAlgorithm(
    _In_ ULONG Algorithm,
    _Outptr_ PWOF_ALGORITHM* AlgorithmVTable
    );
