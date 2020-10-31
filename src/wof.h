#pragma once
#include <ntdll.h>

//////////////////////////////////////////////////////////////////////////
// Structures.
//////////////////////////////////////////////////////////////////////////

typedef struct _WOF_FILE
{
    //
    // Handle to the created file.
    //
    HANDLE FileHandle;

    //
    // Handle to the file:WofCompressedData
    //
    HANDLE CompressedStreamHandle;

    //
    // Total size of uncompressed data.
    //
    ULONG UncompressedSize;

    //
    // Total size of compressed data.
    //
    ULONG CompressedSize;

    //
    // Compression algorithm.
    //
    ULONG Algorithm;
} WOF_FILE, *PWOF_FILE;

//////////////////////////////////////////////////////////////////////////
// Function type definitions.
//////////////////////////////////////////////////////////////////////////

typedef NTSTATUS (NTAPI* PWOF_FEED_DATA_CALLBACK)(
    _Inout_ PVOID Buffer,
    _Inout_ ULONG BufferLength,
    _In_ PVOID Context
    );

//////////////////////////////////////////////////////////////////////////
// Function prototypes.
//////////////////////////////////////////////////////////////////////////

NTSTATUS
NTAPI
WofOpenStream(
    _Out_ PWOF_FILE WofFile,
    _In_ PUNICODE_STRING FilePath,
    _In_ ULONG Algorithm
    );

NTSTATUS
NTAPI
WofCloseStream(
    _In_ PWOF_FILE WofFile
    );

NTSTATUS
NTAPI
WofCompress(
    _In_ PWOF_FILE WofFile,
    _In_ PWOF_FEED_DATA_CALLBACK FeedDataCallback,
    _In_ PVOID FeedDataCallbackContext,
    _In_ ULONG UncompressedSize,
    _In_ ULONG NumberOfThreads
    );
