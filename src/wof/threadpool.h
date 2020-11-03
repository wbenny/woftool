#pragma once
#include <ntdll.h>

#include "algorithm.h"


//////////////////////////////////////////////////////////////////////////
// Definitions.
//////////////////////////////////////////////////////////////////////////

//
// Number of chunks per block.
// Can be tuned.
//

#define WOF_CHUNKS_PER_BLOCK              32


//////////////////////////////////////////////////////////////////////////
// Structures.
//////////////////////////////////////////////////////////////////////////

typedef struct _WOF_WORK_ITEM
{
    //
    // Thread-pool work.
    //

    PTP_WORK ThreadPoolWork;

    //
    // Compression algorithm.
    //

    PWOF_ALGORITHM Algorithm;
    PVOID AlgorithmContext;

    //
    // Uncompressed buffer.
    //

    PVOID UncompressedBuffer;
    ULONG UncompressedBufferSize;
    ULONG UncompressedBufferCapacity;

    //
    // Compressed buffers.
    //

    PVOID CompressedBufferTable[WOF_CHUNKS_PER_BLOCK];
    ULONG CompressedBufferSizeTable[WOF_CHUNKS_PER_BLOCK];
    ULONG ChunkCount;

    //
    // Return status of the work item.
    //

    NTSTATUS Status;
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
    );

VOID
NTAPI
WofpThreadPoolDestroy(
    _In_ PWOF_THREAD_POOL ThreadPool
    );

VOID
NTAPI
WofpThreadPoolSubmitAndWait(
    _In_ PWOF_THREAD_POOL ThreadPool
    );

VOID
NTAPI
WofpThreadPoolWorkCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _Inout_opt_ PVOID Context,
    _Inout_ PTP_WORK Work
    );

//////////////////////////////////////////////////////////////////////////
// Work item functions.
//

NTSTATUS
NTAPI
WofpWorkItemInitialize(
    _Out_ PWOF_WORK_ITEM WorkItem,
    _In_ PWOF_THREAD_POOL ThreadPool,
    _In_ PWOF_ALGORITHM Algorithm
    );

VOID
NTAPI
WofpWorkItemDestroy(
    _In_ PWOF_WORK_ITEM WorkItem
    );

VOID
NTAPI
WofpWorkItemCallback(
    _In_ PWOF_WORK_ITEM WorkItem
    );
