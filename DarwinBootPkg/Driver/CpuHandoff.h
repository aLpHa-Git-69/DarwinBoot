// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef DARWIN_CPU_HANDOFF_H_
#define DARWIN_CPU_HANDOFF_H_

#include "BootPolicy.h"

// Shared with Entry.S. The input and report have separate cache-line ownership;
// the command is on its own UEFI page. The page size here is not XNU's granule.
#define DARWIN_CPU_PAGE_SIZE       0x1000
#define DARWIN_CPU_CONTEXT_PAGES   2
#define DARWIN_CPU_ARGS            0x000
#define DARWIN_CPU_ENTRY           0x008
#define DARWIN_CPU_METHOD          0x010
#define DARWIN_CPU_REPORT          0x800
#define DARWIN_CPU_STATE           0x800
#define DARWIN_CPU_MPIDR           0x808
#define DARWIN_CPU_EL              0x810
#define DARWIN_CPU_MMFR0            0x818
#define DARWIN_CPU_SCTLR            0x820
#define DARWIN_CPU_OFF_RETURN       0x828
#define DARWIN_CPU_COMMAND          0x1000

#define DARWIN_CPU_WAIT             0
#define DARWIN_CPU_START            1
#define DARWIN_CPU_ABORT            2
#define DARWIN_CPU_READY            1
#define DARWIN_CPU_OFF_FAILED       2

#define DARWIN_PSCI_CPU_OFF         0x84000002
#define DARWIN_PSCI_CPU_ON          0xc4000003
#define DARWIN_PSCI_AFFINITY_INFO   0xc4000004
#define DARWIN_PSCI_AFFINITY_ON     0
#define DARWIN_PSCI_AFFINITY_OFF    1
#define DARWIN_PSCI_AFFINITY_PENDING 2

#ifndef __ASSEMBLER__
#include <Uefi.h>

typedef struct {
  UINT64 State;
  UINT64 Mpidr;
  UINT64 CurrentEl;
  UINT64 Mmfr0;
  UINT64 Sctlr;
  UINT64 OffReturn;
} DARWIN_CPU_FEATURES;

typedef struct {
  UINT64                      Args;
  UINT64                      Entry;
  UINT64                      Method;
  UINT8                       InputPadding[DARWIN_CPU_REPORT - 3 * sizeof (UINT64)];
  volatile DARWIN_CPU_FEATURES Report;
  UINT8                       ReportPadding[DARWIN_CPU_PAGE_SIZE - DARWIN_CPU_REPORT - sizeof (DARWIN_CPU_FEATURES)];
  volatile UINT64              Command;
  UINT8                       CommandPadding[DARWIN_CPU_PAGE_SIZE - sizeof (UINT64)];
} DARWIN_CPU_CONTEXT;

STATIC_ASSERT (OFFSET_OF (DARWIN_CPU_CONTEXT, Args) == DARWIN_CPU_ARGS, "CPU args ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_CPU_CONTEXT, Entry) == DARWIN_CPU_ENTRY, "CPU entry ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_CPU_CONTEXT, Method) == DARWIN_CPU_METHOD, "CPU conduit ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_CPU_CONTEXT, Report) == DARWIN_CPU_REPORT, "CPU report ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_CPU_CONTEXT, Report) + OFFSET_OF (DARWIN_CPU_FEATURES, State) == DARWIN_CPU_STATE, "CPU ready ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_CPU_CONTEXT, Report) + OFFSET_OF (DARWIN_CPU_FEATURES, Mpidr) == DARWIN_CPU_MPIDR, "CPU MPIDR ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_CPU_CONTEXT, Report) + OFFSET_OF (DARWIN_CPU_FEATURES, CurrentEl) == DARWIN_CPU_EL, "CPU EL ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_CPU_CONTEXT, Report) + OFFSET_OF (DARWIN_CPU_FEATURES, Mmfr0) == DARWIN_CPU_MMFR0, "CPU MMFR0 ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_CPU_CONTEXT, Report) + OFFSET_OF (DARWIN_CPU_FEATURES, Sctlr) == DARWIN_CPU_SCTLR, "CPU SCTLR ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_CPU_CONTEXT, Report) + OFFSET_OF (DARWIN_CPU_FEATURES, OffReturn) == DARWIN_CPU_OFF_RETURN, "CPU OFF result ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_CPU_CONTEXT, Command) == DARWIN_CPU_COMMAND, "CPU command page ABI");
STATIC_ASSERT (sizeof (DARWIN_CPU_CONTEXT) == DARWIN_CPU_CONTEXT_PAGES * DARWIN_CPU_PAGE_SIZE, "CPU context ABI");

typedef struct {
  EFI_PHYSICAL_ADDRESS Address;
  UINT64               Target;
  UINT32               Method;
  BOOLEAN              Launched;
  INT32                OnReturn;
  INT32                LastAffinity;
} DARWIN_CPU_HANDOFF;

STATIC inline BOOLEAN DarwinCpuFeaturesValid (
  CONST volatile DARWIN_CPU_FEATURES *Features, UINT64 Target, UINT32 PageSize
  ) {
  return (Features->State == DARWIN_CPU_READY) &&
         ((Features->Mpidr & DARWIN_MPIDR_AFFINITY_MASK) == Target) &&
         (Features->CurrentEl == 4) && ((Features->Sctlr & 0x1005) == 0) &&
         DarwinBootGranuleSupported (Features->Mmfr0, PageSize);
}

UINT64 EFIAPI DarwinCurrentEl (VOID);
UINT64 EFIAPI DarwinCurrentMpidr (VOID);
UINT64 EFIAPI DarwinMmfr0 (VOID);
UINT64 EFIAPI DarwinSctlr (VOID);
UINT64 EFIAPI DarwinTcr (VOID);
UINT64 EFIAPI DarwinPfr0 (VOID);
UINT64 EFIAPI DarwinCacheLineSize (VOID);
UINT64 EFIAPI DarwinTranslate (UINT64 Address);
VOID EFIAPI DarwinCacheClean (UINT64 Address, UINT64 Size);
VOID EFIAPI DarwinCacheInvalidate (UINT64 Address, UINT64 Size);
VOID EFIAPI DarwinSendEvent (VOID);
INT32 EFIAPI DarwinPsciCall (UINT64 Method, UINT64 Function, UINT64 Arg1, UINT64 Arg2, UINT64 Arg3);
VOID EFIAPI DarwinColdEntry (UINT64 Context);
extern CONST UINT8 DarwinColdEntryEnd[];
VOID EFIAPI DarwinEnter (UINT64 Args, UINT64 Entry, UINT64 Memory, UINT64 Size, UINT64 VideoBase, UINT64 VideoBytes, UINT64 Context);

EFI_STATUS DarwinCpuPrepare (
  CONST DARWIN_BOOT_POLICY *Policy, UINT64 CurrentCpu, UINT64 Args, UINT64 Entry,
  UINT64 ImageBase, UINT64 ImageSize, DARWIN_CPU_HANDOFF *Handoff
  );
// With ActiveBootServices=FALSE, quiesce using PSCI only and retain the pages:
// a failed ExitBootServices attempt may have partially shut down firmware.
EFI_STATUS DarwinCpuAbort (DARWIN_CPU_HANDOFF *Handoff, BOOLEAN ActiveBootServices);

#endif
#endif
