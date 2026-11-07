// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "CpuHandoff.h"
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define DARWIN_CPU_POLL_COUNT  2000
#define DARWIN_CPU_POLL_US     1000
#define DARWIN_PAR_ADDRESS     0x000ffffffffff000ULL

STATIC
BOOLEAN
IdentityMapped (UINT64 Address, UINT64 Size)
{
  UINT64 End;
  UINT64 Translation;

  if ((Size == 0) || (Address > MAX_UINT64 - Size) ||
      ((Address + Size - 1) & ~(DARWIN_PAR_ADDRESS | EFI_PAGE_MASK)) != 0) { return FALSE; }
  if ((DarwinSctlr () & 1) == 0) { return TRUE; }
  End = Address + Size;
  for (Address &= ~(UINT64)EFI_PAGE_MASK; Address < End; Address += EFI_PAGE_SIZE) {
    Translation = DarwinTranslate (Address);
    if (((Translation & 1) != 0) || ((Translation & DARWIN_PAR_ADDRESS) != Address)) { return FALSE; }
  }
  return TRUE;
}

STATIC
INT32
Affinity (DARWIN_CPU_HANDOFF *Handoff)
{
  Handoff->LastAffinity = DarwinPsciCall (Handoff->Method, DARWIN_PSCI_AFFINITY_INFO, Handoff->Target, 0, 0);
  return Handoff->LastAffinity;
}

EFI_STATUS
DarwinCpuPrepare (
  CONST DARWIN_BOOT_POLICY *Policy, UINT64 CurrentCpu, UINT64 Args, UINT64 Entry,
  UINT64 ImageBase, UINT64 ImageSize, DARWIN_CPU_HANDOFF *Handoff
  )
{
  EFI_STATUS         Status;
  DARWIN_CPU_CONTEXT *Context;
  UINT64             ColdEntry;
  UINT64             ColdEnd;
  UINT64             Line;
  UINTN              Attempt;
  INT32              State;

  if (Handoff->Address != 0) { return EFI_NOT_READY; }
  if (Policy->BootCpu == (CurrentCpu & DARWIN_MPIDR_AFFINITY_MASK)) {
    return DarwinBootGranuleSupported (DarwinMmfr0 (), Policy->PageSize) ? EFI_SUCCESS : EFI_UNSUPPORTED;
  }
  if ((Policy->PsciMethod != DARWIN_PSCI_SMC) && (Policy->PsciMethod != DARWIN_PSCI_HVC)) { return EFI_UNSUPPORTED; }
  if (DarwinCurrentEl () != 4) { return EFI_UNSUPPORTED; }
  ColdEntry = (UINT64)(UINTN)DarwinColdEntry;
  ColdEnd = (UINT64)(UINTN)DarwinColdEntryEnd;
  Line = DarwinCacheLineSize ();
  if ((Line == 0) || (Line > DARWIN_CPU_REPORT) || ((Line & (Line - 1)) != 0) ||
      (ImageBase > MAX_UINT64 - ImageSize) || (ColdEntry & 3) ||
      (ColdEntry < ImageBase) || (ColdEnd <= ColdEntry) || (ColdEnd > ImageBase + ImageSize) ||
      !IdentityMapped (ImageBase, ImageSize)) { return EFI_UNSUPPORTED; }

  Handoff->Target = Policy->BootCpu;
  Handoff->Method = Policy->PsciMethod;
  if (Affinity (Handoff) != DARWIN_PSCI_AFFINITY_OFF) { return EFI_NOT_READY; }
  Status = gBS->AllocatePages (AllocateAnyPages, EfiReservedMemoryType, DARWIN_CPU_CONTEXT_PAGES, &Handoff->Address);
  if (EFI_ERROR (Status)) { Handoff->Address = 0; return Status; }
  if ((Handoff->Address & EFI_PAGE_MASK) ||
      !IdentityMapped (Handoff->Address, sizeof (DARWIN_CPU_CONTEXT))) { return EFI_UNSUPPORTED; }
  Context = (DARWIN_CPU_CONTEXT *)(UINTN)Handoff->Address;
  ZeroMem (Context, sizeof (*Context));
  Context->Args = Args;
  Context->Entry = Entry;
  Context->Method = Handoff->Method;
  DarwinCacheClean (ImageBase, ImageSize);
  DarwinCacheClean (Handoff->Address, sizeof (*Context));

  // Even an error return must resolve OFF before releasing the supplied pages.
  // Firmware may have accepted the entry before returning an unexpected status.
  Handoff->Launched = TRUE;
  Handoff->OnReturn = DarwinPsciCall (Handoff->Method, DARWIN_PSCI_CPU_ON, Handoff->Target, ColdEntry, Handoff->Address);
  if (Handoff->OnReturn != 0) { return EFI_DEVICE_ERROR; }
  for (Attempt = 0; Attempt < DARWIN_CPU_POLL_COUNT; Attempt++) {
    DarwinCacheInvalidate (Handoff->Address + DARWIN_CPU_REPORT, sizeof (Context->Report));
    State = Affinity (Handoff);
    if (Context->Report.State == DARWIN_CPU_READY) {
      if (!DarwinCpuFeaturesValid (&Context->Report, Handoff->Target, Policy->PageSize)) { return EFI_UNSUPPORTED; }
      if (State == DARWIN_PSCI_AFFINITY_ON) { return EFI_SUCCESS; }
    } else if (Context->Report.State != 0) { return EFI_DEVICE_ERROR; }
    if ((State != DARWIN_PSCI_AFFINITY_ON) && (State != DARWIN_PSCI_AFFINITY_PENDING)) { return EFI_DEVICE_ERROR; }
    Status = gBS->Stall (DARWIN_CPU_POLL_US);
    if (EFI_ERROR (Status)) { return Status; }
  }
  return EFI_TIMEOUT;
}

EFI_STATUS
DarwinCpuAbort (DARWIN_CPU_HANDOFF *Handoff, BOOLEAN ActiveBootServices)
{
  DARWIN_CPU_CONTEXT *Context;
  EFI_STATUS         Status;
  UINTN              Attempt;

  if (Handoff->Address == 0) { return EFI_SUCCESS; }
  if (Handoff->Launched) {
    Context = (DARWIN_CPU_CONTEXT *)(UINTN)Handoff->Address;
    Context->Command = DARWIN_CPU_ABORT;
    // Never clean a line written by the uncached target. It could overwrite
    // the fresh feature report with a stale copy in the primary's cache.
    DarwinCacheClean (Handoff->Address + DARWIN_CPU_COMMAND, DARWIN_CPU_PAGE_SIZE);
    DarwinSendEvent ();
    for (Attempt = 0; Attempt < DARWIN_CPU_POLL_COUNT; Attempt++) {
      if (Affinity (Handoff) == DARWIN_PSCI_AFFINITY_OFF) {
        Handoff->Launched = FALSE;
        break;
      }
      if (ActiveBootServices) { gBS->Stall (DARWIN_CPU_POLL_US); }
    }
    if (Handoff->Launched) {
      DarwinCacheInvalidate (Handoff->Address + DARWIN_CPU_REPORT, sizeof (Context->Report));
      return EFI_TIMEOUT;
    }
  }
  if (!ActiveBootServices) { return EFI_SUCCESS; }
  Status = gBS->FreePages (Handoff->Address, DARWIN_CPU_CONTEXT_PAGES);
  if (!EFI_ERROR (Status)) { ZeroMem (Handoff, sizeof (*Handoff)); }
  return Status;
}
