#include "common.h"


//////////////////////////////////////////////////////////////////////////
// Private functions.
//////////////////////////////////////////////////////////////////////////

PVOID
NTAPI
WofpAllocate(
    _In_ SIZE_T Size
    )
{
    return RtlAllocateHeap(RtlProcessHeap(), 0, Size);
}

VOID
NTAPI
WofpFree(
    _In_opt_ PVOID Pointer
    )
{
    RtlFreeHeap(RtlProcessHeap(), 0, Pointer);
}
