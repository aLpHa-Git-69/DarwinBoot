// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef DARWIN_BOOTSTRAP_BANK_H_
#define DARWIN_BOOTSTRAP_BANK_H_

#include <Base.h>
#include "WindowSize.h"

// XNU's initial L2 block mappings: 512 * 4 KiB or 2048 * 16 KiB.
STATIC inline UINT64 DarwinBootstrapBlockSize (UINT32 PageSize) {
  return (PageSize == 4096) ? SIZE_2MB : ((PageSize == 16384) ? SIZE_32MB : 0);
}

STATIC inline BOOLEAN DarwinBootstrapPhaseMatches (UINT64 Virtual, UINT64 Physical, UINT32 PageSize) {
  UINT64 Block = DarwinBootstrapBlockSize (PageSize);
  return (Block != 0) && (((Virtual ^ Physical) & (Block - 1)) == 0);
}

// Admit the prefix as part of the existing ownership budget, not extra RAM.
// Keep the 4 KiB linked base; 16 KiB kernels declare a complete 32 MiB bank.
STATIC inline BOOLEAN DarwinBootstrapBankBase (
  UINT64 LinkedBase, UINT64 End, UINT32 PageSize, UINT64 WindowSize, UINT64 *Base
  ) {
  UINT64 Block = DarwinBootstrapBlockSize (PageSize);
  UINT64 Bank;

  if ((Block == 0) || !DarwinWindowSizeValid (WindowSize) ||
      ((LinkedBase & (PageSize - 1)) != 0) || (End <= LinkedBase)) { return FALSE; }
  Bank = (PageSize == 16384) ? (LinkedBase & ~(Block - 1)) : LinkedBase;
  if ((Bank > MAX_UINT64 - WindowSize) || (End - Bank > WindowSize - SIZE_2MB)) { return FALSE; }
  *Base = Bank;
  return TRUE;
}

// Match the original ceil(2 MiB) + linked offset rule for 4 KiB. For 16 KiB,
// the prepared virtual bank is block-aligned and requires a 32 MiB PA bank.
STATIC inline BOOLEAN DarwinBootstrapCandidate (
  UINT64 Start, UINT64 End, UINT64 VirtualBase, UINT32 PageSize, UINT64 WindowSize, UINT64 *Address
  ) {
  UINT64 Block = DarwinBootstrapBlockSize (PageSize);
  UINT64 Padding;
  UINT64 Offset;
  UINT64 Candidate;

  if ((Block == 0) || !DarwinWindowSizeValid (WindowSize) || (Start >= End)) { return FALSE; }
  Offset = VirtualBase & (Block - 1);
  if ((PageSize == 16384) && (Offset != 0)) { return FALSE; }
  Padding = (0 - Start) & (Block - 1);
  if (Start > MAX_UINT64 - Padding) { return FALSE; }
  Candidate = Start + Padding;
  if (Candidate > MAX_UINT64 - Offset) { return FALSE; }
  Candidate += Offset;
  if ((Candidate > End) || (WindowSize > End - Candidate)) { return FALSE; }
  *Address = Candidate;
  return TRUE;
}

// Checked physical placement, with a cursor relative to the bank. Absolute
// alignment preserves the old 4 KiB path even when its PA base is not 16 KiB aligned.
STATIC inline BOOLEAN DarwinBankReserve (
  UINT64 Base, UINT64 Limit, UINT64 *Cursor, UINT64 Alignment, UINT64 Bytes, UINT64 *Address
  ) {
  UINT64 Padding;
  UINT64 Offset;

  if ((Alignment == 0) || ((Alignment & (Alignment - 1)) != 0) ||
      (Base > MAX_UINT64 - Limit) || (*Cursor > Limit)) { return FALSE; }
  Padding = (0 - (Base + *Cursor)) & (Alignment - 1);
  if (Padding > Limit - *Cursor) { return FALSE; }
  Offset = *Cursor + Padding;
  if (Bytes > Limit - Offset) { return FALSE; }
  *Address = Base + Offset;
  *Cursor = Offset + Bytes;
  return TRUE;
}

STATIC inline BOOLEAN DarwinConventionalRangeEnd (UINT64 Start, UINT64 Pages, UINT64 *End) {
  if ((Start & (SIZE_4KB - 1)) || (Pages == 0) || (Pages > (MAX_UINT64 - Start) / SIZE_4KB)) { return FALSE; }
  *End = Start + Pages * SIZE_4KB;
  return TRUE;
}

#endif
