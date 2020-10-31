# woftool

`woftool` is a proof-of-concept utility that allows you to take a source file and store its WOF compressed version
as a different file. Only the XPRESS algorithm is implemented, but you can choose from all of the supported block
sizes (4K, 8K, 16K).

`woftool` is also multithreaded and allows you to specify number of threads to use during the compression.

### Motivation

Recently I wanted to download ~10TB+ of text data, however I didn't have 10TB of spare storage hanging around. That led
me to [re-discovering FS compression][tweet]. During the research I found out that since Windows 10 you can actually
use so-called "WOF compression", which offers multithreaded compression, higher compression ratio and multiple
compression algorithms.

### WOF compression

WOF (Windows Overlay Filter) compression is greatly described by Raymond Chen [on his blog][oldnewthing], but I'll
try to summarize some important points:

* WOF compression is **not** a NTFS native file system compression
* WOF compression is handled by a file system driver (`wof.sys`), that is usually loaded in regular default Windows
  installation
* From NTFS point of view, WOF compressed files have these characteristics:
  * They are _sparse_ files with no data
  * File size is set to the size of uncompressed data (but because they're sparse with no data, they take no disk space)
  * They have `:WofCompressedData` Alternate Data Stream, which contains the actual compressed data
  * They have `IO_REPARSE_TAG_WOF` reparse point set
* Decompression of WOF compressed files is handled transparently by the `wof.sys` driver - application doesn't have to
  care if the file is compressed or not
* However, if you try to write to the WOF compressed file, the file is transparently decompressed (and the compressed
  file is replaced with its decompressed version)
* There is no option to mark folder as "WOF compressed" and expect that every written file there will be compressed

From this information we can gather that the WOF compression is useful for files that aren't modified.

### `compact.exe`

Windows has already built-in utility for compressing files - `compact.exe`. It has been part of Windows for long time
and before Windows 10 it could only enable/disable the standard NTFS compression.

Starting with Windows 10, `compact.exe` has been extended and supports creating WOF compressed files. You can compress
a file with this command:

```
compact.exe /c /exe:lzx "file.bin"`
```

... and decompress it with:

```
compact.exe /u /exe:lzx "file.bin"`
```

The `/exe` parameter has a bit misleading name - this parameter serves as a selector of the compression algorithm.
You can chose from:

* XPRESS4K  (fastest) (default)
* XPRESS8K
* XPRESS16K
* LZX (most compact)

Note that when uncompressing WOF compressed file (`/u`), you need to specify the `/exe` parameter again, otherwise the
`compact.exe` will try to reset the standard NTFS compression.

### Internals

Internally, the `compact.exe` does nothing else than open the file and issue `DeviceIoControl`:

```c
struct
{
  WOF_EXTERNAL_INFO WofInfo;
  FILE_PROVIDER_EXTERNAL_INFO_V1 FileInfo;
} Buffer;

Buffer.WofInfo.Version = WOF_CURRENT_VERSION;                   // 1
Buffer.WofInfo.Provider = WOF_PROVIDER_FILE;                    // 2
Buffer.FileInfo.Version = FILE_PROVIDER_CURRENT_VERSION;        // 1
Buffer.FileInfo.Algorithm = FILE_PROVIDER_COMPRESSION_XPRESS4K;
Buffer.FileInfo.Flags = 0;

//
// Valid Algorithm values:
//
// #define FILE_PROVIDER_COMPRESSION_XPRESS4K   (0x00000000)
// #define FILE_PROVIDER_COMPRESSION_LZX        (0x00000001)
// #define FILE_PROVIDER_COMPRESSION_XPRESS8K   (0x00000002)
// #define FILE_PROVIDER_COMPRESSION_XPRESS16K  (0x00000003)
//

DeviceIoControl(FileHandle,
                FSCTL_SET_EXTERNAL_BACKING,
                &Buffer,
                sizeof(Buffer),
                NULL,
                0,
                &BytesReturned,
                NULL);

```

That's it. This IOCTL will be captured by `wof.sys`, which does the heavy lifting.

The actual content of the `:WofCompressedData` stream consists of 2 parts:
* "Chunk table"
* Actual compressed data

The chunk table is simply an array of `uint32_t` elements and each item contains an offset to the next compressed chunk.
One might ask - what if the compressed file is bigger than 4GB? The answer is - if the _uncompressed_ file is bigger
than 4GB, then the chunk table actually consists of `uint64_t`.

The actual compressed data are simply concatenated compressed data blocks. If any compressed block size is higher than
the uncompressed block, then the block is stored as uncompressed data.

You can find more information on
[`FSCTL_SET_EXTERNAL_BACKING`][FSCTL_SET_EXTERNAL_BACKING],
[`WOF_EXTERNAL_INFO`][WOF_EXTERNAL_INFO] and
[`FILE_PROVIDER_EXTERNAL_INFO_V1`][FILE_PROVIDER_EXTERNAL_INFO_V1]
on MSDN.

### Problem

You might have spotted one limitation - there doesn't exist a way how to take a source file and compress it into another
file. Everything is done in-place.

My specific use-case was to download the data and compress them onto USB-connected external hard drive (yes, the
spinning one). However, it's not possible to compress a file on one disk, and transfer such compressed file on
another disk - it'll get decompressed during the copy. The only option seemed to be to store all files on the external
drive and continuously compress it there. However, it has obvious disadvantages - it'll be painfully slow.

One might ask - couldn't you just use some kind of backup tool, that backs up files with all Alternate Data Streams?
The answer is, unfortunatelly, **no**.

The reason it's not possible is that the `wof.sys` filter driver actually **hides** the `:WofCompressedData`
stream - it's not visible by any tool. Also, any attempt to directly create or open `:WofCompressedData` results in
`STATUS_ACCESS_DENIED`.

### Solution

What about the other way around? What if we tried to create `:WofCompressedData` stream and fill it ourselves?

As I mentioned earlier, creation of `:WofCompressedData` is not possible. However, what is possible is to create
stream with any other name, and then rename it to `:WofCompressedData`!

But there is another obstacle - the WOF compressed file is also defined by the `IO_REPARSE_TAG_WOF` reparse point.
You can set reparse point on a file by issuing `FSCTL_SET_REPARSE_POINT` on it.

If you'd be guessing that `wof.sys` is filtering this IOCTL and returning `STATUS_ACCESS_DENIED`, you'd be actually
right. But for some reason `wof.sys` doesn't filter `FSCTL_SET_REPARSE_POINT_EX` IOCTL - and it is actually possible
to create the reparse point this way.

### Usage

```
woftool.exe <source> <destination> <algorithm> <threads>
```

Valid values for <algorithm>:
* xpress4k
* xpress8k
* xpress16k

Examples:

```
woftool.exe "source.txt" "destination.txt" xpress16k 1
woftool.exe "C:\test.txt" "D:\test.txt" xpress8k 4
```

### Compilation

Because [Native API header files for the Process Hacker project][phnt] is attached as a git submodule, you must not
forget to fetch it:

`git clone --recurse-submodules https://github.com/wbenny/woftool`

After that, compile **woftools** using Visual Studio 2019. Solution file is included. No other dependencies are
required.

### Implementation

The WOF compression is handled by pair of `wof.c`/`wof.h` files, which depends only on `ntdll.dll`. Multithreading
is handled by using the `Tp` thread-pool routines exported by the `ntdll.dll`.

### Remarks

Please note that this is a proof-of-concept implementation and thus it's possible that it may contain bugs.
Do not take the validity of the created files as granted, as they may be corrupted. I take no responsibility for any
data loss.

### Special thanks

Special thanks goes to [jonasLyk][jonasLyk] who nudged me into right way during my research and implementation.

### License

This software is open-source under the MIT license. See the LICENSE.txt file in this repository.

Dependencies are licensed by their own licenses.

If you find this project interesting, you can buy me a coffee

```
  BTC 3GwZMNGvLCZMi7mjL8K6iyj6qGbhkVMNMF
  LTC MQn5YC7bZd4KSsaj8snSg4TetmdKDkeCYk
```

  [tweet]: <https://twitter.com/PetrBenes/status/1318004862362288128>
  [oldnewthing]: <https://devblogs.microsoft.com/oldnewthing/20190618-00/?p=102597>
  [FSCTL_SET_EXTERNAL_BACKING]: <https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/fsctl-set-external-backing>
  [WOF_EXTERNAL_INFO]: <https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_wof_external_info>
  [FILE_PROVIDER_EXTERNAL_INFO_V1]: <https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_provider_external_info_v1>
  [phnt]: <https://github.com/processhacker/phnt>
  [jonasLyk]: <https://twitter.com/jonasLyk>
