#include "xpress.h"
#include "../common.h"


//////////////////////////////////////////////////////////////////////////
// Private functions.
//////////////////////////////////////////////////////////////////////////

NTSTATUS
NTAPI
WofpAlgorithmXpressInitialize(
    _Outptr_ PVOID* AlgorithmContext
    )
{
    NTSTATUS Status;

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
        return Status;
    }

    PVOID WorkSpace;
    WorkSpace = WofpAllocate(CompressBufferWorkSpaceSize);

    if (!WorkSpace)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *AlgorithmContext = WorkSpace;

    return Status;
}

VOID
NTAPI
WofpAlgorithmXpressDestroy(
    _In_ PVOID AlgorithmContext
    )
{
    WofpFree(AlgorithmContext);
}

NTSTATUS
NTAPI
WofpAlgorithmXpressCompress(
    _In_ PVOID AlgorithmContext,
    _In_ PVOID UncompressedChunkBuffer,
    _In_ ULONG UncompressedChunkBufferSize,
    _In_ PVOID CompressedChunkBuffer,
    _In_ ULONG CompressedChunkBufferSize,
    _In_ PULONG FinalCompressedSize
    )
{
    NTSTATUS Status;

    PVOID WorkSpace = AlgorithmContext;

    Status = RtlCompressBuffer(COMPRESSION_FORMAT_XPRESS_HUFF,
                               UncompressedChunkBuffer,
                               UncompressedChunkBufferSize,
                               CompressedChunkBuffer,
                               CompressedChunkBufferSize,
                               0,
                               FinalCompressedSize,
                               WorkSpace);

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

    return Status;
}
