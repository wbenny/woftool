// #define TEST_TEST_TEST

#if defined(TEST_TEST_TEST)
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#endif

#include "wof.h"
#include "common.h"
#include "algorithm.h"
#include "io.h"
#include "threadpool.h"


//////////////////////////////////////////////////////////////////////////
// Public interface.
//////////////////////////////////////////////////////////////////////////

NTSTATUS
NTAPI
WofOpenStream(
    _Out_ PWOF_FILE WofFile,
    _In_ PUNICODE_STRING FilePath,
    _In_ ULONG FileProviderCompression
    )
{
    NTSTATUS Status;

    //
    // Create the file.
    //

    HANDLE FileHandle;
    Status = WofpIoCreateFile(FilePath, &FileHandle);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    //
    // Create the ":WofCompressedDat_" stream.
    //

    HANDLE CompressedStreamHandle;
    Status = WofpIoCreateCompressedStream(FileHandle,
                                          &CompressedStreamHandle);

    if (!NT_SUCCESS(Status))
    {
        NtClose(FileHandle);
        return Status;
    }

    //
    // Rename the ":WofCompressedDat_" to ":WofCompressedData".
    //

    Status = WofpIoRenameCompressedStream(CompressedStreamHandle);

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
    WofFile->FileProviderCompression = FileProviderCompression;

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

    Status = WofpIoSetEndOfFile(WofFile->FileHandle, &UncompressedSize);

    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    //
    // Tag the file with the WOF reparse point.
    //

    Status = WofpIoSetReparsePoint(WofFile->FileHandle,
                                   WofFile->FileProviderCompression);

Exit:
    //
    // Close all handles.
    //

    NtClose(WofFile->CompressedStreamHandle);
    NtClose(WofFile->FileHandle);

    return Status;
}

#if defined(TEST_TEST_TEST)
FILE* __f;
#endif

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

    PWOF_ALGORITHM Algorithm;
    Status = WofpFileProviderCompressionToAlgorithm(WofFile->FileProviderCompression,
                                                    &Algorithm);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    //
    // Initialize thread-pool.
    //

    WOF_THREAD_POOL ThreadPool;
    Status = WofpThreadPoolInitialize(&ThreadPool,
                                      Algorithm,
                                      NumberOfThreads);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    //
    // Initialize the compressed batch buffer.
    // Compressed chunks are concatenated to this buffer and this buffer
    // is then written to the ":WofCompressedData" stream.
    //

    ULONG CompressedBatchBufferSize = Algorithm->ChunkSize * WOF_CHUNKS_PER_BLOCK * NumberOfThreads;
    PUCHAR CompressedBatchBuffer = WofpAllocate(CompressedBatchBufferSize);

    if (!CompressedBatchBuffer)
    {
        WofpThreadPoolDestroy(&ThreadPool);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Initialize chunk table.
    // Chunk table holds offsets of compressed data for all chunk indexes.
    //
    // #TODO: Check for ChunkCount overflow.
    //
    ULONG ChunkCount = ((UncompressedSize + Algorithm->ChunkSize - 1) / Algorithm->ChunkSize);
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

    LARGE_INTEGER FileOffset;
    FileOffset.QuadPart = ChunkTableSizeInBytes;

    Status = WofpIoSetFilePosition(WofFile->CompressedStreamHandle,
                                   &FileOffset);

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
                              WorkItem->CompressedBufferSizeTable[ChunkIndex]);

                BatchBufferOffset += WorkItem->CompressedBufferSizeTable[ChunkIndex];
                CompressedSize += WorkItem->CompressedBufferSizeTable[ChunkIndex];

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

        Status = WofpIoWriteFile(WofFile->CompressedStreamHandle,
                                 CompressedBatchBuffer,
                                 BatchBufferOffset);

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

    FileOffset.QuadPart = 0;
    Status = WofpIoSetFilePosition(WofFile->CompressedStreamHandle,
                                   &FileOffset);

    //
    // Write the chunk table.
    //

#if defined(TEST_TEST_TEST)
    fwrite(ChunkTable, 1, ChunkTableSizeInBytes, __f);
    fclose(__f);
#endif

    Status = WofpIoWriteFile(WofFile->CompressedStreamHandle,
                             ChunkTable,
                             ChunkTableSizeInBytes);

    WofFile->UncompressedSize = UncompressedSize;
    WofFile->CompressedSize = CompressedSize;

Cleanup:
    WofpFree(ChunkTable);
    WofpFree(CompressedBatchBuffer);
    WofpThreadPoolDestroy(&ThreadPool);

    return Status;
}
