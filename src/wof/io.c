#include "io.h"


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


//////////////////////////////////////////////////////////////////////////
// Variables.
//////////////////////////////////////////////////////////////////////////

UNICODE_STRING WofpCompressedData = RTL_CONSTANT_STRING(WOF_COMPRESSED_DATA);
UNICODE_STRING WofpCompressedDat_ = RTL_CONSTANT_STRING(WOF_COMPRESSED_DAT_);


//////////////////////////////////////////////////////////////////////////
// Private functions.
//////////////////////////////////////////////////////////////////////////

NTSTATUS
NTAPI
WofpIoCreateFile(
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

NTSTATUS
NTAPI
WofpIoWriteFile(
    _In_ HANDLE FileHandle,
    _In_ PVOID Buffer,
    _In_ ULONG Length
    )
{
    IO_STATUS_BLOCK IoStatusBlock;

    return NtWriteFile(FileHandle,
                       NULL,
                       NULL,
                       NULL,
                       &IoStatusBlock,
                       Buffer,
                       Length,
                       NULL,
                       NULL);
}

NTSTATUS
NTAPI
WofpIoSetEndOfFile(
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

NTSTATUS
NTAPI
WofpIoSetFilePosition(
    _In_ HANDLE FileHandle,
    _In_ PLARGE_INTEGER CurrentByteOffset
    )
{
    IO_STATUS_BLOCK IoStatusBlock;

    FILE_POSITION_INFORMATION PositionInformation;
    PositionInformation.CurrentByteOffset = *CurrentByteOffset;

    return NtSetInformationFile(FileHandle,
                                &IoStatusBlock,
                                &PositionInformation,
                                sizeof(PositionInformation),
                                FilePositionInformation);
}

NTSTATUS
NTAPI
WofpIoSetReparsePoint(
    _In_ HANDLE FileHandle,
    _In_ ULONG FileProviderCompression
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
    WofReparseBuffer->FileProviderInfo.Algorithm = FileProviderCompression;
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

NTSTATUS
NTAPI
WofpIoCreateCompressedStream(
    _In_ HANDLE FileHandle,
    _Out_ PHANDLE CompressedStreamHandle
    )
{
    NTSTATUS Status;

    OBJECT_ATTRIBUTES ObjectAttributes;
    InitializeObjectAttributes(&ObjectAttributes,
                               &WofpCompressedDat_,
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

NTSTATUS
NTAPI
WofpIoRenameCompressedStream(
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
    FileInformation->FileNameLength = WofpCompressedData.Length;
    RtlCopyMemory(FileInformation->FileName,
                  WofpCompressedData.Buffer,
                  WofpCompressedData.Length);

    IO_STATUS_BLOCK IoStatusBlock;
    Status = NtSetInformationFile(CompressedStreamHandle,
                                  &IoStatusBlock,
                                  FileInformation,
                                  sizeof(FileInformationBuffer),
                                  FileRenameInformation);

    return Status;
}

