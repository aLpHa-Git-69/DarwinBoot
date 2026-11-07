// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef DARWIN_BOOT_POLICY_H_
#define DARWIN_BOOT_POLICY_H_

#define DARWIN_MPIDR_AFFINITY_MASK  0xff00ffffffULL
#define DARWIN_PSCI_NONE           0
#define DARWIN_PSCI_SMC            1
#define DARWIN_PSCI_HVC            2

#define DARWIN_POLICY_ARGS_ABI     0
#define DARWIN_POLICY_PAGE_SIZE    1
#define DARWIN_POLICY_BOOT_CPU     2
#define DARWIN_POLICY_PSCI_METHOD  3

#define DARWIN_ADT_CHOSEN          1
#define DARWIN_ADT_PSCI            4

#ifndef __ASSEMBLER__
#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

typedef struct {
  UINT32 ArgsAbi;
  UINT32 PageSize;
  UINT64 BootCpu;
  UINT32 PsciMethod;
  UINT32 SeenProperties;
  UINT32 SeenNodes;
} DARWIN_BOOT_POLICY;

STATIC inline VOID DarwinBootPolicyInit (DARWIN_BOOT_POLICY *Policy, UINT64 CurrentCpu) {
  ZeroMem (Policy, sizeof (*Policy));
  Policy->ArgsAbi = 23;
  Policy->PageSize = 4096;
  Policy->BootCpu = CurrentCpu & DARWIN_MPIDR_AFFINITY_MASK;
}

// ADT properties are typed little-endian scalars or one exact NUL-terminated
// PSCI method. Apply a property once, and leave the policy intact on refusal.
STATIC inline BOOLEAN DarwinBootPolicySet (
  DARWIN_BOOT_POLICY *Policy, UINT32 Property, CONST UINT8 *Data, UINTN Length
  ) {
  UINT32 Value32;
  UINT64 Value64;
  UINT32 Bit;

  if ((Property > DARWIN_POLICY_PSCI_METHOD) || (Data == NULL)) { return FALSE; }
  Bit = 1U << Property;
  if ((Policy->SeenProperties & Bit) != 0) { return FALSE; }
  switch (Property) {
    case DARWIN_POLICY_ARGS_ABI:
      if (Length != sizeof (UINT32)) { return FALSE; }
      Value32 = ReadUnaligned32 ((CONST UINT32 *)Data);
      if ((Value32 != 22) && (Value32 != 23)) { return FALSE; }
      Policy->ArgsAbi = Value32;
      break;
    case DARWIN_POLICY_PAGE_SIZE:
      if (Length != sizeof (UINT32)) { return FALSE; }
      Value32 = ReadUnaligned32 ((CONST UINT32 *)Data);
      if ((Value32 != 4096) && (Value32 != 16384)) { return FALSE; }
      Policy->PageSize = Value32;
      break;
    case DARWIN_POLICY_BOOT_CPU:
      if (Length != sizeof (UINT64)) { return FALSE; }
      Value64 = ReadUnaligned64 ((CONST UINT64 *)Data);
      if ((Value64 & ~DARWIN_MPIDR_AFFINITY_MASK) != 0) { return FALSE; }
      Policy->BootCpu = Value64;
      break;
    case DARWIN_POLICY_PSCI_METHOD:
      if (Length != 4) { return FALSE; }
      if (CompareMem (Data, "smc", 4) == 0) { Policy->PsciMethod = DARWIN_PSCI_SMC; }
      else if (CompareMem (Data, "hvc", 4) == 0) { Policy->PsciMethod = DARWIN_PSCI_HVC; }
      else { return FALSE; }
      break;
  }
  Policy->SeenProperties |= Bit;
  return TRUE;
}

STATIC inline BOOLEAN DarwinBootPolicyNode (DARWIN_BOOT_POLICY *Policy, UINTN Path) {
  UINT32 Bit;

  if ((Path != DARWIN_ADT_CHOSEN) && (Path != DARWIN_ADT_PSCI)) { return TRUE; }
  Bit = 1U << Path;
  if ((Policy->SeenNodes & Bit) != 0) { return FALSE; }
  Policy->SeenNodes |= Bit;
  return TRUE;
}

// Only defined architectural encodings are accepted, including LPA2 support.
STATIC inline BOOLEAN DarwinBootGranuleSupported (UINT64 Mmfr0, UINT32 PageSize) {
  UINT64 Granule;

  if (PageSize == 4096) {
    Granule = (Mmfr0 >> 28) & 15;
    return (Granule == 0) || (Granule == 1);
  }
  if (PageSize == 16384) {
    Granule = (Mmfr0 >> 20) & 15;
    return (Granule == 1) || (Granule == 2);
  }
  return FALSE;
}

#endif
#endif
