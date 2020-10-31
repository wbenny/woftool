#include "wof.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct _WOF_FEEDER
{
    FILE* File;
} WOF_FEEDER, *PWOF_FEEDER;

static
NTSTATUS
NTAPI
WofFeederCallback(
    _In_ PVOID Buffer,
    _In_ ULONG BufferLength,
    _In_ PVOID Context
    )
{
    PWOF_FEEDER WofFeeder = (PWOF_FEEDER)(Context);

    fread(Buffer, 1, BufferLength, WofFeeder->File);

    return STATUS_SUCCESS;
}

int
wmain(
    int argc,
    wchar_t* argv[]
    )
{
    if (argc != 5)
    {
        fwprintf(stderr, L"Usage:\n");
        fwprintf(stderr, L"    woftool <source> <destination> <algorithm> <threads>\n");
        fwprintf(stderr, L"\n");
        fwprintf(stderr, L"Valid values for <algorithm>:\n");
        fwprintf(stderr, L"    xpress4k\n");
        fwprintf(stderr, L"    xpress8k\n");
        fwprintf(stderr, L"    xpress16k\n");

        return EXIT_FAILURE;
    }

    NTSTATUS Status;

    PWCHAR ArgSourcePath = argv[1];
    PWCHAR ArgDestinationPath = argv[2];
    PWCHAR ArgAlgorithm = argv[3];
    PWCHAR ArgThreads = argv[4];

    //
    // Check <algorithm> parameter.
    //

    ULONG Algorithm;
    if (!wcscmp(ArgAlgorithm, L"xpress4k"))
    {
        Algorithm = FILE_PROVIDER_COMPRESSION_XPRESS4K;
    }
    else if (!wcscmp(ArgAlgorithm, L"xpress8k"))
    {
        Algorithm = FILE_PROVIDER_COMPRESSION_XPRESS8K;
    }
    else if (!wcscmp(ArgAlgorithm, L"xpress16k"))
    {
        Algorithm = FILE_PROVIDER_COMPRESSION_XPRESS16K;
    }
    else
    {
        fwprintf(stderr, L"Invalid algorithm\n");
        return EXIT_FAILURE;
    }

    //
    // Check <threads> parameter.
    //

    ULONG NumberOfThreads = _wtoi(ArgThreads);
    if (NumberOfThreads == 0)
    {
        fwprintf(stderr, L"Invalid thread count\n");
        return EXIT_FAILURE;
    }

    //
    // Create data feeder and open the source file.
    //

    WOF_FEEDER WofFeeder;
    RtlZeroMemory(&WofFeeder, sizeof(WofFeeder));

    if (_wfopen_s(&WofFeeder.File, ArgSourcePath, L"rb") != 0)
    {
        fprintf(stderr, "Cannot open the source file\n");
        return EXIT_FAILURE;
    }

    fseek(WofFeeder.File, 0, SEEK_END);

    ULONGLONG UncompressedSize = ftell(WofFeeder.File);

    if (UncompressedSize >= (4ull * 1024 * 1024 * 1024))
    {
        fprintf(stderr, "Cannot compress files >= 4GB\n");
        fclose(WofFeeder.File);
        return EXIT_FAILURE;
    }

    fseek(WofFeeder.File, 0, SEEK_SET);

    UNICODE_STRING DestinationPath;
    DestinationPath.MaximumLength = (USHORT)(sizeof(L"\\??\\") + wcslen(ArgDestinationPath) * sizeof(WCHAR));
    DestinationPath.Length = 0;
    DestinationPath.Buffer = malloc(DestinationPath.MaximumLength);

    RtlAppendUnicodeToString(&DestinationPath, L"\\??\\");
    RtlAppendUnicodeToString(&DestinationPath, ArgDestinationPath);

    //
    // Open the destination file.
    //

    WOF_FILE WofFile;
    Status = WofOpenStream(&WofFile, &DestinationPath, Algorithm);

    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr, "Cannot create the destination file\n");
        fclose(WofFeeder.File);
        return EXIT_FAILURE;
    }

    //
    // Compress the data.
    //

    Status = WofCompress(&WofFile,
                         &WofFeederCallback,
                         &WofFeeder,
                         (ULONG)(UncompressedSize),
                         NumberOfThreads);

    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr, "Error during compression\n");
    }
    else
    {
        wprintf(L"Uncompressed size   : %u bytes\n", WofFile.UncompressedSize);
        wprintf(L"Compressed size     : %u bytes\n", WofFile.CompressedSize);
        wprintf(L"Compression ratio   : %.2fx\n", (double)(WofFile.UncompressedSize) / WofFile.CompressedSize);
    }

    //
    // Close the destination file.
    //

    Status = WofCloseStream(&WofFile);

    fclose(WofFeeder.File);

    return EXIT_SUCCESS;
}
