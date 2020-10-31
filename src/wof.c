#define TEST_TEST_TEST

#if defined(TEST_TEST_TEST)
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#endif

#include "wof.h"

//////////////////////////////////////////////////////////////////////////
// Definitions.
//////////////////////////////////////////////////////////////////////////

//
// Definitions copied from ntifs.h.
//

#define REPARSE_DATA_EX_FLAG_GIVEN_TAG_OR_NONE              (0x00000001)

#define REPARSE_GUID_DATA_BUFFER_EX_HEADER_SIZE \
    UFIELD_OFFSET(REPARSE_DATA_BUFFER_EX, ReparseGuidDataBuffer.GenericReparseBuffer)

#define REPARSE_DATA_BUFFER_EX_HEADER_SIZE \
    UFIELD_OFFSET(REPARSE_DATA_BUFFER_EX, ReparseDataBuffer.GenericReparseBuffer)

//
// Number of chunks per block.
// Can be tuned.
//

#define WOF_CHUNKS_PER_BLOCK              32

//
// Placeholder name of the Alternate Data Stream.
// This stream is created first and then later renamed to ":WofCompressedData",
// because direct creation of ":WofCompressedData" stream is blocked by the
// wof.sys filter driver.
//

#define WOF_COMPRESSED_DAT_               L":WofCompressedDat_"

//
// Name of the Alternate Data Stream for the WOF compressed data.
//

#define WOF_COMPRESSED_DATA               L":WofCompressedData"

//////////////////////////////////////////////////////////////////////////
// Structures.
//////////////////////////////////////////////////////////////////////////

//
// Structures copied from ntifs.h.
//

typedef struct _REPARSE_DATA_BUFFER_EX
{
    ULONG     Flags;
    ULONG     ExistingReparseTag;
    GUID      ExistingReparseGuid;
    ULONGLONG Reserved;
    union
    {
        REPARSE_DATA_BUFFER      ReparseDataBuffer;
        REPARSE_GUID_DATA_BUFFER ReparseGuidDataBuffer;
    } DUMMYUNIONNAME;
} REPARSE_DATA_BUFFER_EX, *PREPARSE_DATA_BUFFER_EX;

typedef struct _WOF_REPARSE_BUFFER
{
    WOF_EXTERNAL_INFO WofExternalInfo;
    FILE_PROVIDER_EXTERNAL_INFO_V1 FileProviderInfo;
} WOF_REPARSE_BUFFER, *PWOF_REPARSE_BUFFER;

//
// The compression can run in multiple threads managed by the NTDLL's
// Tp (thread-pool) module.
//

typedef struct _WOF_WORK_ITEM
{
    //
    // Thread-pool work.
    //
    PTP_WORK ThreadPoolWork;

    //
    // RtlCompress* support workspace.
    //
    PVOID WorkSpace;

    //
    // Buffer that holds uncompressed data for this work item.
    //
    PVOID UncompressedBuffer;
    ULONG UncompressedBufferSize;
    ULONG UncompressedBufferCapacity;

    //
    // Compression algorithm.
    //
    ULONG Algorithm;

    //
    // Array of compressed chunk buffers.
    //
    PVOID CompressedBufferTable[WOF_CHUNKS_PER_BLOCK];

    //
    // Array of compressed chunk sizes.
    //
    ULONG ChunkTable[WOF_CHUNKS_PER_BLOCK];

    //
    // Number of chunks in this compression block.
    //
    ULONG ChunkCount;

    //
    // Size of uncompressed chunk.
    //
    ULONG ChunkSize;
} WOF_WORK_ITEM, *PWOF_WORK_ITEM;

typedef struct _WOF_THREAD_POOL
{
    //
    // Thread-pool support.
    //
    PTP_POOL Pool;
    PTP_CLEANUP_GROUP CleanupGroup;
    TP_CALLBACK_ENVIRON CallbackEnvironment;

    //
    // Array of work items.
    //
    PWOF_WORK_ITEM WorkItemTable;
    ULONG WorkItemCount;
    ULONG WorkItemActive;
} WOF_THREAD_POOL, *PWOF_THREAD_POOL;

//////////////////////////////////////////////////////////////////////////
// Function prototypes.
//////////////////////////////////////////////////////////////////////////

static
PVOID
NTAPI
WofpAllocate(
    _In_ SIZE_T Size
    );

static
VOID
NTAPI
WofpFree(
    _In_ PVOID Pointer
    );

static
NTSTATUS
NTAPI
WofpCreateFile(
    _In_ PUNICODE_STRING FilePath,
    _Out_ PHANDLE FileHandle
    );

static
NTSTATUS
NTAPI
WofpCreateCompressedStream(
    _In_ HANDLE FileHandle,
    _Out_ PHANDLE CompressedStreamHandle
    );

static
NTSTATUS
NTAPI
WofpRenameCompressedStream(
    _In_ HANDLE CompressedStreamHandle
    );

static
NTSTATUS
NTAPI
WofpSetEndOfFile(
    _In_ HANDLE FileHandle,
    _In_ PLARGE_INTEGER FileSize
    );

static
NTSTATUS
NTAPI
WofpSetReparsePoint(
    _In_ HANDLE FileHandle,
    _In_ ULONG Algorithm
    );

static
FORCEINLINE
NTSTATUS
NTAPI
WofpAlgorithmToChunkSize(
    _In_ ULONG Algorithm,
    _Out_ PULONG ChunkSize
    );

static
NTSTATUS
NTAPI
WofpCompressBlockXpress(
    _In_ PWOF_WORK_ITEM WorkItem
    );

//
// Thread pool functions.
//

static
NTSTATUS
NTAPI
WofpInitializeThreadPool(
    _Out_ PWOF_THREAD_POOL ThreadPool,
    _In_ ULONG NumberOfThreads,
    _In_ ULONG Algorithm
    );

static
VOID
NTAPI
WofpDestroyThreadPool(
    _In_ PWOF_THREAD_POOL ThreadPool
    );

static
VOID
NTAPI
WofpThreadPoolSubmitAndWait(
    _In_ PWOF_THREAD_POOL ThreadPool
    );

static
VOID
NTAPI
WofpThreadPoolWorkCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _Inout_opt_ PVOID Context,
    _Inout_ PTP_WORK Work
    );

//
// Work item functions.
//

static
NTSTATUS
NTAPI
WofpInitializeWorkItem(
    _Inout_ PWOF_WORK_ITEM WorkItem,
    _In_ PWOF_THREAD_POOL ThreadPool,
    _In_ ULONG Algorithm
    );

static
VOID
NTAPI
WofpDestroyWorkItem(
    _In_ PWOF_WORK_ITEM WorkItem
    );

//////////////////////////////////////////////////////////////////////////
// Variables.
//////////////////////////////////////////////////////////////////////////

UNICODE_STRING WofCompressedData = RTL_CONSTANT_STRING(WOF_COMPRESSED_DATA);
UNICODE_STRING WofCompressedDat_ = RTL_CONSTANT_STRING(WOF_COMPRESSED_DAT_);

#if defined(TEST_TEST_TEST)
FILE* __f;
#endif

//
// Helper functions.
//

static
PVOID
NTAPI
WofpAllocate(
    _In_ SIZE_T Size
    )
{
    return RtlAllocateHeap(RtlProcessHeap(), 0, Size);
}

static
VOID
NTAPI
WofpFree(
    _In_ PVOID Pointer
    )
{
    RtlFreeHeap(RtlProcessHeap(), 0, Pointer);
}

static
NTSTATUS
NTAPI
WofpCreateFile(
    _In_ PUNICODE_STRING FilePath,
    _Out_ PHANDLE FileHandle
    )
{
    NTSTATUS Status;

    OBJECT_ATTRIBUTES ObjectAttributes;
    InitializeObjectAttributes(&ObjectAttributes,
                               FilePath,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    IO_STATUS_BLOCK IoStatusBlock;
    Status = NtCreateFile(FileHandle,
                          GENERIC_WRITE | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ,
                          FILE_OPEN_IF,
                          FILE_SYNCHRONOUS_IO_ALERT,
                          NULL,
                          0);

    return Status;
}

static
NTSTATUS
NTAPI
WofpCreateCompressedStream(
    _In_ HANDLE FileHandle,
    _Out_ PHANDLE CompressedStreamHandle
    )
{
    NTSTATUS Status;

    OBJECT_ATTRIBUTES ObjectAttributes;
    InitializeObjectAttributes(&ObjectAttributes,
                               &WofCompressedDat_,
                               OBJ_CASE_INSENSITIVE,
                               FileHandle,
                               NULL);

    IO_STATUS_BLOCK IoStatusBlock;
    Status = NtCreateFile(CompressedStreamHandle,
                          GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN_IF,
                          FILE_SYNCHRONOUS_IO_ALERT,
                          NULL,
                          0);

    return Status;
}

static
NTSTATUS
NTAPI
WofpRenameCompressedStream(
    _In_ HANDLE CompressedStreamHandle
    )
{
    NTSTATUS Status;

    UCHAR FileInformationBuffer[
        sizeof(FILE_RENAME_INFORMATION) - sizeof(WCHAR)   // Subtract the `WCHAR FileName[1]`.
      + sizeof(WOF_COMPRESSED_DATA)     - sizeof(WCHAR)   // Subtract the L'\0'.
    ];

    PFILE_RENAME_INFORMATION FileInformation = (PFILE_RENAME_INFORMATION)(FileInformationBuffer);
    FileInformation->ReplaceIfExists = FALSE;
    FileInformation->RootDirectory = NULL;
    FileInformation->FileNameLength = WofCompressedData.Length;
    RtlCopyMemory(FileInformation->FileName,
                  WofCompressedData.Buffer,
                  WofCompressedData.Length);

    IO_STATUS_BLOCK IoStatusBlock;
    Status = NtSetInformationFile(CompressedStreamHandle,
                                  &IoStatusBlock,
                                  FileInformation,
                                  sizeof(FileInformationBuffer),
                                  FileRenameInformation);

    return Status;
}

static
NTSTATUS
NTAPI
WofpSetEndOfFile(
    _In_ HANDLE FileHandle,
    _In_ PLARGE_INTEGER FileSize
    )
{
    NTSTATUS Status;

    LARGE_INTEGER CapturedFileSize = *FileSize;

    //
    // First, make this file sparse.
    //

    IO_STATUS_BLOCK IoStatusBlock;
    Status = NtFsControlFile(FileHandle,
                             NULL,
                             NULL,
                             NULL,
                             &IoStatusBlock,
                             FSCTL_SET_SPARSE,
                             NULL,
                             0,
                             NULL,
                             0);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    //
    // Set the EndOfFileInformation.
    //

    FILE_END_OF_FILE_INFORMATION EndOfFileInformation;
    EndOfFileInformation.EndOfFile = CapturedFileSize;

    Status = NtSetInformationFile(FileHandle,
                                  &IoStatusBlock,
                                  &EndOfFileInformation,
                                  sizeof(EndOfFileInformation),
                                  FileEndOfFileInformation);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    //
    // Set the ValidDataLengthInformation.
    //

    FILE_VALID_DATA_LENGTH_INFORMATION ValidDataLengthInformation;
    ValidDataLengthInformation.ValidDataLength = CapturedFileSize;

    Status = NtSetInformationFile(FileHandle,
                                  &IoStatusBlock,
                                  &ValidDataLengthInformation,
                                  sizeof(ValidDataLengthInformation),
                                  FileValidDataLengthInformation);

    return Status;
}

static
NTSTATUS
NTAPI
WofpSetReparsePoint(
    _In_ HANDLE FileHandle,
    _In_ ULONG Algorithm
    )
{
    NTSTATUS Status;

    UCHAR ByteBuffer[REPARSE_DATA_BUFFER_EX_HEADER_SIZE + sizeof(WOF_REPARSE_BUFFER)];
    RtlZeroMemory(ByteBuffer, sizeof(ByteBuffer));

    PREPARSE_DATA_BUFFER_EX ReparseDataEx = (PREPARSE_DATA_BUFFER_EX)(ByteBuffer);
    ReparseDataEx->Flags = REPARSE_DATA_EX_FLAG_GIVEN_TAG_OR_NONE;
    ReparseDataEx->ExistingReparseTag = 0;
    ReparseDataEx->Reserved = 0;

    PREPARSE_DATA_BUFFER ReparseData = &ReparseDataEx->ReparseDataBuffer;
    ReparseData->ReparseTag = IO_REPARSE_TAG_WOF;
    ReparseData->ReparseDataLength = sizeof(WOF_REPARSE_BUFFER);
    ReparseData->Reserved = 0;

    PWOF_REPARSE_BUFFER WofReparseBuffer = (PWOF_REPARSE_BUFFER)(ReparseData->GenericReparseBuffer.DataBuffer);
    WofReparseBuffer->WofExternalInfo.Version = WOF_CURRENT_VERSION;
    WofReparseBuffer->WofExternalInfo.Provider = WOF_PROVIDER_FILE;
    WofReparseBuffer->FileProviderInfo.Version = FILE_PROVIDER_CURRENT_VERSION;
    WofReparseBuffer->FileProviderInfo.Algorithm = Algorithm;
    WofReparseBuffer->FileProviderInfo.Flags = 0;

    IO_STATUS_BLOCK IoStatusBlock;
    Status = NtFsControlFile(FileHandle,
                             NULL,
                             NULL,
                             NULL,
                             &IoStatusBlock,
                             FSCTL_SET_REPARSE_POINT_EX,
                             ByteBuffer,
                             sizeof(ByteBuffer),
                             NULL,
                             0);

    return Status;
}

static
FORCEINLINE
NTSTATUS
NTAPI
WofpAlgorithmToChunkSize(
    _In_ ULONG Algorithm,
    _Out_ PULONG ChunkSize
    )
{
    switch (Algorithm)
    {
        case FILE_PROVIDER_COMPRESSION_XPRESS4K:
            *ChunkSize = 4096;
            break;

        case FILE_PROVIDER_COMPRESSION_XPRESS8K:
            *ChunkSize = 8192;
            break;

        case FILE_PROVIDER_COMPRESSION_XPRESS16K:
            *ChunkSize = 16384;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
WofpCompressBlockXpress(
    _In_ PWOF_WORK_ITEM WorkItem
    )
{
    RTL_ASSERT(WorkItem->UncompressedBufferSize <= WorkItem->UncompressedBufferCapacity);

    NTSTATUS Status;

    PUCHAR UncompressedBuffer = (PUCHAR)(WorkItem->UncompressedBuffer);

    ULONG RemainingBytes = WorkItem->UncompressedBufferSize;
    ULONG ChunkNumber = 0;

    while (ChunkNumber < WOF_CHUNKS_PER_BLOCK && RemainingBytes > 0)
    {
        PVOID UncompressedChunkBuffer = &UncompressedBuffer[ChunkNumber * WorkItem->ChunkSize];
        ULONG UncompressedChunkBufferSize = min(WorkItem->ChunkSize, RemainingBytes);
        PVOID CompressedChunkBuffer = WorkItem->CompressedBufferTable[ChunkNumber];
        ULONG CompressedChunkBufferSize = UncompressedChunkBufferSize - 1;
        PULONG FinalCompressedSize = &WorkItem->ChunkTable[ChunkNumber];

        Status = RtlCompressBuffer(COMPRESSION_FORMAT_XPRESS_HUFF,
                                   UncompressedChunkBuffer,
                                   UncompressedChunkBufferSize,
                                   CompressedChunkBuffer,
                                   CompressedChunkBufferSize,
                                   0,
                                   FinalCompressedSize,
                                   WorkItem->WorkSpace);

        if (Status == STATUS_BUFFER_ALL_ZEROS)
        {
            *FinalCompressedSize = 0;
        }
        else if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            RtlMoveMemory(CompressedChunkBuffer,
                          UncompressedChunkBuffer,
                          UncompressedChunkBufferSize);

            *FinalCompressedSize = UncompressedChunkBufferSize;
        }

        RemainingBytes -= UncompressedChunkBufferSize;
        ChunkNumber += 1;
    }

    WorkItem->ChunkCount = ChunkNumber;

    return STATUS_SUCCESS;
}

//
// Thread pool functions.
//

static
NTSTATUS
NTAPI
WofpInitializeThreadPool(
    _Out_ PWOF_THREAD_POOL ThreadPool,
    _In_ ULONG NumberOfThreads,
    _In_ ULONG Algorithm
    )
{
    NTSTATUS Status;

    //
    // Initialize thread-pool.
    //

    TpInitializeCallbackEnviron(&ThreadPool->CallbackEnvironment);

    Status = TpAllocPool(&ThreadPool->Pool, NULL);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = TpSetPoolMinThreads(ThreadPool->Pool, NumberOfThreads);

    if (!NT_SUCCESS(Status))
    {
        TpReleasePool(ThreadPool->Pool);
        return Status;
    }

    TpSetPoolMaxThreads(ThreadPool->Pool, NumberOfThreads);

    TpSetCallbackThreadpool(&ThreadPool->CallbackEnvironment, ThreadPool->Pool);

    Status = TpAllocCleanupGroup(&ThreadPool->CleanupGroup);

    if (!NT_SUCCESS(Status))
    {
        TpReleasePool(ThreadPool->Pool);
        return Status;
    }

    //
    // Initialize WorkItemTable.
    //

    PWOF_WORK_ITEM WorkItemTable;
    WorkItemTable = (PWOF_WORK_ITEM)(WofpAllocate(sizeof(WOF_WORK_ITEM) * NumberOfThreads));

    if (!WorkItemTable)
    {
        TpReleasePool(ThreadPool->Pool);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (ULONG Index = 0; Index < NumberOfThreads; Index += 1)
    {
        Status = WofpInitializeWorkItem(&WorkItemTable[Index],
                                        ThreadPool,
                                        Algorithm);

        if (!NT_SUCCESS(Status))
        {
            while (Index--)
            {
                WofpDestroyWorkItem(&WorkItemTable[Index]);
            }

            TpReleasePool(ThreadPool->Pool);
            return Status;
        }
    }

    ThreadPool->WorkItemTable = WorkItemTable;
    ThreadPool->WorkItemCount = NumberOfThreads;
    ThreadPool->WorkItemActive = 0;

    return Status;
}

static
VOID
NTAPI
WofpDestroyThreadPool(
    _In_ PWOF_THREAD_POOL ThreadPool
    )
{
    for (ULONG Index = 0; Index < ThreadPool->WorkItemActive; Index += 1)
    {
        TpReleaseWork(ThreadPool->WorkItemTable[Index].ThreadPoolWork);
    }

    TpReleaseCleanupGroupMembers(ThreadPool->CleanupGroup, FALSE, NULL);
    TpReleaseCleanupGroup(ThreadPool->CleanupGroup);
    TpReleasePool(ThreadPool->Pool);
}

static
VOID
NTAPI
WofpThreadPoolSubmitAndWait(
    _In_ PWOF_THREAD_POOL ThreadPool
    )
{
    ULONG Index;

    for (Index = 0; Index < ThreadPool->WorkItemActive; Index += 1)
    {
        TpPostWork(ThreadPool->WorkItemTable[Index].ThreadPoolWork);
    }

    for (Index = 0; Index < ThreadPool->WorkItemActive; Index += 1)
    {
        TpWaitForWork(ThreadPool->WorkItemTable[Index].ThreadPoolWork, FALSE);
    }
}

static
VOID
NTAPI
WofpThreadPoolWorkCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _Inout_opt_ PVOID Context,
    _Inout_ PTP_WORK Work
    )
{
    NTSTATUS Status;

    PWOF_WORK_ITEM WorkItem = (PWOF_WORK_ITEM)(Context);
    Status = WofpCompressBlockXpress(WorkItem);
}

//
// Work item functions.
//

static
NTSTATUS
NTAPI
WofpInitializeWorkItem(
    _Inout_ PWOF_WORK_ITEM WorkItem,
    _In_ PWOF_THREAD_POOL ThreadPool,
    _In_ ULONG Algorithm
    )
{
    NTSTATUS Status;

    RtlZeroMemory(WorkItem, sizeof(*WorkItem));

    //
    // Initialize thread-pool work.
    //

    Status = TpAllocWork(&WorkItem->ThreadPoolWork,
                         &WofpThreadPoolWorkCallback,
                         WorkItem,
                         &ThreadPool->CallbackEnvironment);

    if (!NT_SUCCESS(Status))
    {
        WofpDestroyWorkItem(WorkItem);
        return Status;
    }

    //
    // Initialize compression workspace.
    //

    ULONG CompressBufferWorkSpaceSize;
    ULONG CompressFragmentWorkSpaceSize;
    Status = RtlGetCompressionWorkSpaceSize(COMPRESSION_FORMAT_XPRESS_HUFF,
                                            &CompressBufferWorkSpaceSize,
                                            &CompressFragmentWorkSpaceSize);

    if (!NT_SUCCESS(Status))
    {
        WofpDestroyWorkItem(WorkItem);
        return Status;
    }

    WorkItem->WorkSpace = WofpAllocate(CompressBufferWorkSpaceSize);

    if (!WorkItem->WorkSpace)
    {
        WofpDestroyWorkItem(WorkItem);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Initialize uncompressed buffer.
    //

    ULONG ChunkSize;
    Status = WofpAlgorithmToChunkSize(Algorithm, &ChunkSize);

    if (!NT_SUCCESS(Status))
    {
        WofpDestroyWorkItem(WorkItem);
        return Status;
    }

    ULONG UncompressedBufferCapacity = ChunkSize * WOF_CHUNKS_PER_BLOCK;

    WorkItem->UncompressedBuffer = WofpAllocate(UncompressedBufferCapacity);

    if (!WorkItem->UncompressedBuffer)
    {
        WofpDestroyWorkItem(WorkItem);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    WorkItem->UncompressedBufferSize = 0;
    WorkItem->UncompressedBufferCapacity = UncompressedBufferCapacity;

    //
    // Initialize the algorithm.
    //

    WorkItem->Algorithm = Algorithm;

    //
    // Initialize compressed buffer table.
    //

    for (ULONG Index = 0; Index < WOF_CHUNKS_PER_BLOCK; Index += 1)
    {
        WorkItem->CompressedBufferTable[Index] = WofpAllocate(ChunkSize);

        if (!WorkItem->CompressedBufferTable[Index])
        {
            WofpDestroyWorkItem(WorkItem);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    //
    // Initialize chunk table.
    //
    // NB: We've zero-ed out the memory at the beginning of this function,
    //     therefore zero-ing it out again doesn't make sense.
    //
    // RtlZeroMemory(WorkItem->ChunkTable, sizeof(WorkItem->ChunkTable));
    //

    WorkItem->ChunkCount = 0;
    WorkItem->ChunkSize = ChunkSize;

    return Status;
}

static
VOID
NTAPI
WofpDestroyWorkItem(
    _In_ PWOF_WORK_ITEM WorkItem
    )
{
    for (ULONG Index = 0; Index < WOF_CHUNKS_PER_BLOCK; Index += 1)
    {
        WofpFree(WorkItem->CompressedBufferTable[Index]);
    }

    WofpFree(WorkItem->UncompressedBuffer);
    WofpFree(WorkItem->WorkSpace);

    TpReleaseWork(WorkItem->ThreadPoolWork);

    // RtlZeroMemory(WorkItem, sizeof(*WorkItem));
}

//
// Public interface.
//

NTSTATUS
NTAPI
WofOpenStream(
    _Out_ PWOF_FILE WofFile,
    _In_ PUNICODE_STRING FilePath,
    _In_ ULONG Algorithm
    )
{
    NTSTATUS Status;

    //
    // Create the file.
    //

    HANDLE FileHandle;
    Status = WofpCreateFile(FilePath, &FileHandle);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    //
    // Create the ":WofCompressedDat_" stream.
    //

    HANDLE CompressedStreamHandle;
    Status = WofpCreateCompressedStream(FileHandle, &CompressedStreamHandle);

    if (!NT_SUCCESS(Status))
    {
        NtClose(FileHandle);
        return Status;
    }

    //
    // Rename the ":WofCompressedDat_" to ":WofCompressedData".
    //

    Status = WofpRenameCompressedStream(CompressedStreamHandle);

    if (!NT_SUCCESS(Status))
    {
        NtClose(CompressedStreamHandle);
        NtClose(FileHandle);
    }

    //
    // Finally, initialize the context.
    //

    WofFile->FileHandle = FileHandle;
    WofFile->CompressedStreamHandle = CompressedStreamHandle;
    WofFile->UncompressedSize = 0;
    WofFile->CompressedSize = 0;
    WofFile->Algorithm = Algorithm;

    return Status;
}

NTSTATUS
NTAPI
WofCloseStream(
    _In_ PWOF_FILE WofFile
    )
{
    NTSTATUS Status;

    //
    // Set size of the file to match the size of the uncompressed data.
    //

    LARGE_INTEGER UncompressedSize;
    UncompressedSize.QuadPart = WofFile->UncompressedSize;

    Status = WofpSetEndOfFile(WofFile->FileHandle, &UncompressedSize);

    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    //
    // Tag the file with the WOF reparse point.
    //

    Status = WofpSetReparsePoint(WofFile->FileHandle, WofFile->Algorithm);

Exit:
    //
    // Close all handles.
    //

    NtClose(WofFile->CompressedStreamHandle);
    NtClose(WofFile->FileHandle);

    return Status;
}

NTSTATUS
NTAPI
WofCompress(
    _In_ PWOF_FILE WofFile,
    _In_ PWOF_FEED_DATA_CALLBACK FeedDataCallback,
    _In_ PVOID FeedDataCallbackContext,
    _In_ ULONG UncompressedSize,
    _In_ ULONG NumberOfThreads
    )
{
    NTSTATUS Status;

    //
    // Figure out chunk size for the specified algorithm.
    //

    ULONG ChunkSize;
    Status = WofpAlgorithmToChunkSize(WofFile->Algorithm, &ChunkSize);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    //
    // Initialize thread-pool.
    //

    WOF_THREAD_POOL ThreadPool;
    Status = WofpInitializeThreadPool(&ThreadPool,
                                      NumberOfThreads,
                                      WofFile->Algorithm);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    //
    // Initialize the compressed batch buffer.
    // Compressed chunks are concatenated to this buffer and this buffer
    // is then written to the ":WofCompressedData" stream.
    //

    ULONG CompressedBatchBufferSize = ChunkSize * WOF_CHUNKS_PER_BLOCK * NumberOfThreads;
    PUCHAR CompressedBatchBuffer = WofpAllocate(CompressedBatchBufferSize);

    if (!CompressedBatchBuffer)
    {
        WofpDestroyThreadPool(&ThreadPool);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Initialize chunk table.
    // Chunk table holds offsets of compressed data for all chunk indexes.
    //
    // #TODO: Check for ChunkCount overflow.
    //
    ULONG ChunkCount = ((UncompressedSize + ChunkSize - 1) / ChunkSize);
    PULONG ChunkTable = (PULONG)(WofpAllocate(ChunkCount * sizeof(ULONG)));

    if (!ChunkTable)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    //
    // Adjust position of compressed data.
    //

    ULONG ChunkTableSizeInBytes;
    ChunkTableSizeInBytes = (ChunkCount - 1) * sizeof(ULONG);

#if defined(TEST_TEST_TEST)
    __f = fopen("WofCompressedData.bin", "wb+");
    fseek(__f, ChunkTableSizeInBytes, SEEK_SET);
#endif

    IO_STATUS_BLOCK IoStatusBlock;

    FILE_POSITION_INFORMATION PositionInformation;
    PositionInformation.CurrentByteOffset.QuadPart = ChunkTableSizeInBytes;

    Status = NtSetInformationFile(WofFile->CompressedStreamHandle,
                                  &IoStatusBlock,
                                  &PositionInformation,
                                  sizeof(PositionInformation),
                                  FilePositionInformation);

    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    //
    // Start the compression loop.
    //

    ULONG RemainingBytes = UncompressedSize;
    ULONG BlockNumber = 0;
    ULONG CompressedSize = 0;

    while (RemainingBytes > 0)
    {
        //
        // Reset the number of active work items.
        //

        ThreadPool.WorkItemActive = 0;

        //
        // Initialize buffers of work items.
        //

        for (ULONG Index = 0; Index < NumberOfThreads && RemainingBytes > 0; Index += 1)
        {
            PWOF_WORK_ITEM WorkItem = &ThreadPool.WorkItemTable[Index];

            WorkItem->UncompressedBufferSize = min(WorkItem->UncompressedBufferCapacity,
                                                   RemainingBytes);

            Status = FeedDataCallback(WorkItem->UncompressedBuffer,
                                      WorkItem->UncompressedBufferSize,
                                      FeedDataCallbackContext);

            if (!NT_SUCCESS(Status))
            {
                goto Cleanup;
            }

            RemainingBytes -= WorkItem->UncompressedBufferSize;

            ThreadPool.WorkItemActive += 1;
        }

        //
        // Run the compression in thread-pool workers and wait for completion.
        //

        WofpThreadPoolSubmitAndWait(&ThreadPool);

        //
        // Copy compressed chunks to the batch buffer.
        //

        ULONG BatchBufferOffset = 0;

        for (ULONG Index = 0; Index < ThreadPool.WorkItemActive; Index += 1)
        {
            PWOF_WORK_ITEM WorkItem = &ThreadPool.WorkItemTable[Index];

            for (ULONG ChunkIndex = 0; ChunkIndex < WorkItem->ChunkCount; ChunkIndex += 1)
            {
                RtlCopyMemory(&CompressedBatchBuffer[BatchBufferOffset],
                              WorkItem->CompressedBufferTable[ChunkIndex],
                              WorkItem->ChunkTable[ChunkIndex]);

                BatchBufferOffset += WorkItem->ChunkTable[ChunkIndex];
                CompressedSize += WorkItem->ChunkTable[ChunkIndex];

                ChunkTable[BlockNumber * WOF_CHUNKS_PER_BLOCK + ChunkIndex] = CompressedSize;
            }

            BlockNumber += 1;
        }

        //
        // Write the batch buffer to the compressed stream.
        //

#if defined(TEST_TEST_TEST)
        fwrite(CompressedBatchBuffer, 1, BatchBufferOffset, __f);
#endif

        Status = NtWriteFile(WofFile->CompressedStreamHandle,
                             NULL,
                             NULL,
                             NULL,
                             &IoStatusBlock,
                             CompressedBatchBuffer,
                             BatchBufferOffset,
                             NULL,
                             NULL);

        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
    }

    //
    // Adjust position back to the beginning.
    //

#if defined(TEST_TEST_TEST)
    fseek(__f, 0, SEEK_SET);
#endif

    PositionInformation.CurrentByteOffset.QuadPart = 0;

    Status = NtSetInformationFile(WofFile->CompressedStreamHandle,
                                  &IoStatusBlock,
                                  &PositionInformation,
                                  sizeof(PositionInformation),
                                  FilePositionInformation);

    //
    // Write the chunk table.
    //

#if defined(TEST_TEST_TEST)
    fwrite(ChunkTable, 1, ChunkTableSizeInBytes, __f);
    fclose(__f);
#endif

    Status = NtWriteFile(WofFile->CompressedStreamHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         ChunkTable,
                         ChunkTableSizeInBytes,
                         NULL,
                         NULL);

    WofFile->UncompressedSize = UncompressedSize;
    WofFile->CompressedSize = CompressedSize;

Cleanup:
    WofpFree(ChunkTable);
    WofpFree(CompressedBatchBuffer);
    WofpDestroyThreadPool(&ThreadPool);

    return Status;
}
