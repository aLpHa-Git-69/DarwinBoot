// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef DARWIN_BOOT_ARGS_H_
#define DARWIN_BOOT_ARGS_H_

#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

// pexpert/pexpert/arm64/boot.h ABIs; all fields are little-endian.
typedef struct {
  UINT16  Revision;
  UINT16  Version;
  UINT32  Padding;
  UINT64  VirtualBase;
  UINT64  PhysicalBase;
  UINT64  MemorySize;
  UINT64  TopOfKernelData;
  UINT64  Video[6];
  UINT32  MachineType;
  UINT32  TreePadding;
  UINT64  DeviceTree;
  UINT32  DeviceTreeLength;
  CHAR8   CommandLine[608];
  UINT32  FlagsPadding;
  UINT64  BootFlags;
  UINT64  MemorySizeActual;
} DARWIN_BOOT_ARGS22;

STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS22, DeviceTree) == 0x60, "Darwin 22 DT ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS22, Video) == 0x28, "Darwin 22 video ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS22, CommandLine) == 0x6c, "Darwin 22 command line ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS22, BootFlags) == 0x2d0, "Darwin 22 flags ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS22, MemorySizeActual) == 0x2d8, "Darwin 22 memory ABI");
STATIC_ASSERT (sizeof (DARWIN_BOOT_ARGS22) == 736, "Darwin 22 boot_args ABI");

typedef struct {
  UINT16  Revision;
  UINT16  Version;
  UINT32  Padding;
  UINT64  VirtualBase;
  UINT64  PhysicalBase;
  UINT64  MemorySize;
  UINT64  TopOfKernelData;
  UINT64  Video[6];
  UINT32  MachineType;
  UINT32  TreePadding;
  UINT64  DeviceTree;
  UINT32  DeviceTreeLength;
  CHAR8   CommandLine[1024];
  UINT32  FlagsPadding;
  UINT64  BootFlags;
  UINT64  MemorySizeActual;
} DARWIN_BOOT_ARGS23;

// Keep the existing name for callers using the default ABI.
typedef DARWIN_BOOT_ARGS23 DARWIN_BOOT_ARGS;

STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS23, DeviceTree) == 0x60, "DT ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS23, Video) == 0x28, "video ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS23, CommandLine) == 0x6c, "command line ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS23, BootFlags) == 0x470, "flags ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS23, MemorySizeActual) == 0x478, "memory ABI");
STATIC_ASSERT (sizeof (DARWIN_BOOT_ARGS23) == 0x480, "boot_args ABI");

STATIC inline UINTN DarwinBootArgsSize (UINT32 Abi) {
  return (Abi == 22) ? sizeof (DARWIN_BOOT_ARGS22) : ((Abi == 23) ? sizeof (DARWIN_BOOT_ARGS23) : 0);
}

STATIC inline UINTN DarwinBootCommandCapacity (UINT32 Abi) {
  return (Abi == 22) ? sizeof (((DARWIN_BOOT_ARGS22 *)0)->CommandLine) :
         ((Abi == 23) ? sizeof (((DARWIN_BOOT_ARGS23 *)0)->CommandLine) : 0);
}

// Validate the whole input before writing: an oversized line is never truncated.
STATIC inline BOOLEAN DarwinBootCommandLine (CHAR8 *Output, UINTN Capacity, CONST CHAR16 *Input) {
  UINTN Length;
  UINTN Index;

  if ((Output == NULL) || (Capacity == 0)) { return FALSE; }
  Length = 0;
  if (Input != NULL) {
    for (; Length < Capacity; Length++) {
      if (Input[Length] == 0) { break; }
      if (Input[Length] > 0x7f) { return FALSE; }
    }
    if (Length == Capacity) { return FALSE; }
  }
  ZeroMem (Output, Capacity);
  for (Index = 0; Index < Length; Index++) { Output[Index] = (CHAR8)Input[Index]; }
  return TRUE;
}

// Values uses the default type as a template. Copy only the shared prefix;
// the trailing fields must use the selected ABI's offsets, not the template's.
STATIC inline BOOLEAN DarwinBootArgsWrite (
  VOID *Output, UINTN Capacity, UINT32 Abi, CONST DARWIN_BOOT_ARGS23 *Values, CONST CHAR8 *CommandLine
  ) {
  UINTN Size;
  UINTN Limit;
  UINTN Length;
  UINTN Flags;

  Size = DarwinBootArgsSize (Abi);
  Limit = DarwinBootCommandCapacity (Abi);
  if ((Output == NULL) || (Values == NULL) || (Size == 0) || (Capacity < Size)) { return FALSE; }
  Length = 0;
  if (CommandLine != NULL) {
    for (; Length < Limit; Length++) {
      if (CommandLine[Length] == 0) { break; }
      if ((UINT8)CommandLine[Length] > 0x7f) { return FALSE; }
    }
    if (Length == Limit) { return FALSE; }
  }
  Flags = (Abi == 22) ? OFFSET_OF (DARWIN_BOOT_ARGS22, BootFlags) : OFFSET_OF (DARWIN_BOOT_ARGS23, BootFlags);
  ZeroMem (Output, Size);
  CopyMem (Output, Values, OFFSET_OF (DARWIN_BOOT_ARGS23, CommandLine));
  if (Length != 0) { CopyMem ((UINT8 *)Output + OFFSET_OF (DARWIN_BOOT_ARGS23, CommandLine), CommandLine, Length); }
  WriteUnaligned64 ((UINT64 *)((UINT8 *)Output + Flags), Values->BootFlags);
  WriteUnaligned64 ((UINT64 *)((UINT8 *)Output + Flags + sizeof (UINT64)), Values->MemorySizeActual);
  return TRUE;
}

#endif
