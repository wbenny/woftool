#pragma once
#include <ntdll.h>


//////////////////////////////////////////////////////////////////////////
// Private functions.
//////////////////////////////////////////////////////////////////////////

NTSTATUS
NTAPI
WofpIoCreateFile(
    _In_ PUNICODE_STRING FilePath,
    _Out_ PHANDLE FileHandle
    );

NTSTATUS
NTAPI
WofpIoWriteFile(
    _In_ HANDLE FileHandle,
    _In_ PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
NTAPI
WofpIoSetEndOfFile(
    _In_ HANDLE FileHandle,
    _In_ PLARGE_INTEGER FileSize
    );

NTSTATUS
NTAPI
WofpIoSetFilePosition(
    _In_ HANDLE FileHandle,
    _In_ PLARGE_INTEGER CurrentByteOffset
    );

NTSTATUS
NTAPI
WofpIoSetReparsePoint(
    _In_ HANDLE FileHandle,
    _In_ ULONG FileProviderCompression
    );

NTSTATUS
NTAPI
WofpIoCreateCompressedStream(
    _In_ HANDLE FileHandle,
    _Out_ PHANDLE CompressedStreamHandle
    );

NTSTATUS
NTAPI
WofpIoRenameCompressedStream(
    _In_ HANDLE CompressedStreamHandle
    );
