#include "threadpool.h"
#include "common.h"


//////////////////////////////////////////////////////////////////////////
// Private functions.
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// Thread pool functions.
//

NTSTATUS
NTAPI
WofpThreadPoolInitialize(
    _Out_ PWOF_THREAD_POOL ThreadPool,
    _In_ PWOF_ALGORITHM Algorithm,
    _In_ ULONG NumberOfThreads
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
        Status = WofpWorkItemInitialize(&WorkItemTable[Index],
                                        ThreadPool,
                                        Algorithm);

        if (!NT_SUCCESS(Status))
        {
            while (Index--)
            {
                WofpWorkItemDestroy(&WorkItemTable[Index]);
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

VOID
NTAPI
WofpThreadPoolDestroy(
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

VOID
NTAPI
WofpThreadPoolSubmitAndWait(
    _In_ PWOF_THREAD_POOL ThreadPool
    )
{
    //
    // Submit all work items to the thread-pool.
    //

    for (ULONG Index = 0; Index < ThreadPool->WorkItemActive; Index += 1)
    {
        TpPostWork(ThreadPool->WorkItemTable[Index].ThreadPoolWork);
    }

    //
    // Wait for all work items to complete.
    //

    for (ULONG Index = 0; Index < ThreadPool->WorkItemActive; Index += 1)
    {
        TpWaitForWork(ThreadPool->WorkItemTable[Index].ThreadPoolWork, FALSE);
    }
}

VOID
NTAPI
WofpThreadPoolWorkCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _Inout_opt_ PVOID Context,
    _Inout_ PTP_WORK Work
    )
{
    //
    // Delegate work item to the WorkItem function.
    //

    PWOF_WORK_ITEM WorkItem = (PWOF_WORK_ITEM)(Context);

    RTL_ASSERT(WorkItem != NULL);

    WofpWorkItemCallback(WorkItem);
}

//////////////////////////////////////////////////////////////////////////
// Work item functions.
//

NTSTATUS
NTAPI
WofpWorkItemInitialize(
    _Out_ PWOF_WORK_ITEM WorkItem,
    _In_ PWOF_THREAD_POOL ThreadPool,
    _In_ PWOF_ALGORITHM Algorithm
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
        WofpWorkItemDestroy(WorkItem);
        return Status;
    }

    //
    // Initialize algorithm context.
    //

    WorkItem->Algorithm = Algorithm;

    Status = WorkItem->Algorithm->VTable->Initialize(&WorkItem->AlgorithmContext);

    if (!NT_SUCCESS(Status))
    {
        WofpWorkItemDestroy(WorkItem);
        return Status;
    }

    //
    // Initialize uncompressed buffer.
    //

    ULONG UncompressedBufferCapacity = Algorithm->ChunkSize * WOF_CHUNKS_PER_BLOCK;

    WorkItem->UncompressedBuffer = WofpAllocate(UncompressedBufferCapacity);

    if (!WorkItem->UncompressedBuffer)
    {
        WofpWorkItemDestroy(WorkItem);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    WorkItem->UncompressedBufferSize = 0;
    WorkItem->UncompressedBufferCapacity = UncompressedBufferCapacity;

    //
    // Initialize compressed buffer table.
    //

    for (ULONG Index = 0; Index < WOF_CHUNKS_PER_BLOCK; Index += 1)
    {
        WorkItem->CompressedBufferTable[Index] = WofpAllocate(Algorithm->ChunkSize);

        if (!WorkItem->CompressedBufferTable[Index])
        {
            WofpWorkItemDestroy(WorkItem);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    //
    // NB: We've zero-ed out the memory at the beginning of this function,
    //     therefore zero-ing it out again doesn't make sense.
    //
    // RtlZeroMemory(WorkItem->ChunkTable, sizeof(WorkItem->ChunkTable));
    //

    WorkItem->ChunkCount = 0;

    WorkItem->Status = STATUS_SUCCESS;

    return Status;
}

VOID
NTAPI
WofpWorkItemDestroy(
    _In_ PWOF_WORK_ITEM WorkItem
    )
{
    for (ULONG Index = 0; Index < WOF_CHUNKS_PER_BLOCK; Index += 1)
    {
        WofpFree(WorkItem->CompressedBufferTable[Index]);
    }

    WofpFree(WorkItem->UncompressedBuffer);

    if (
        WorkItem->Algorithm &&
        WorkItem->Algorithm->VTable &&
        WorkItem->Algorithm->VTable->Destroy
        )
    {
        WorkItem->Algorithm->VTable->Destroy(WorkItem->AlgorithmContext);
    }

    TpReleaseWork(WorkItem->ThreadPoolWork);
}

VOID
NTAPI
WofpWorkItemCallback(
    _In_ PWOF_WORK_ITEM WorkItem
    )
{
    RTL_ASSERT(WorkItem->UncompressedBufferSize <= WorkItem->UncompressedBufferCapacity);

    NTSTATUS Status;

    PUCHAR UncompressedBuffer = (PUCHAR)(WorkItem->UncompressedBuffer);

    ULONG RemainingBytes = WorkItem->UncompressedBufferSize;
    ULONG ChunkNumber = 0;

    WorkItem->Status = STATUS_SUCCESS;

    //
    // Compress this block of data in chunks.
    //

    while (ChunkNumber < WOF_CHUNKS_PER_BLOCK && RemainingBytes > 0)
    {
        //
        // Assign values for chunk of uncompressed buffer.
        //

        PVOID  UncompressedChunkBuffer      = &UncompressedBuffer[ChunkNumber * WorkItem->Algorithm->ChunkSize];
        ULONG  UncompressedChunkBufferSize  = min(WorkItem->Algorithm->ChunkSize, RemainingBytes);

        //
        // Assign values for chunk of compressed buffer.
        //

        PVOID  CompressedChunkBuffer        = WorkItem->CompressedBufferTable[ChunkNumber];
        ULONG  CompressedChunkBufferSize    = UncompressedChunkBufferSize - 1;

        PULONG FinalCompressedSize          = &WorkItem->CompressedBufferSizeTable[ChunkNumber];

        //
        // Compress the chunk.
        //

        Status = WorkItem->Algorithm->VTable->Compress(WorkItem->AlgorithmContext,
                                                       UncompressedChunkBuffer,
                                                       UncompressedChunkBufferSize,
                                                       CompressedChunkBuffer,
                                                       CompressedChunkBufferSize,
                                                       FinalCompressedSize);

        if (!NT_SUCCESS(Status))
        {
            WorkItem->Status = Status;
            break;
        }

        RemainingBytes -= UncompressedChunkBufferSize;
        ChunkNumber += 1;
    }

    WorkItem->ChunkCount = ChunkNumber;
}
