// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <AsmMacroLib.h>
#include "CpuHandoff.h"

ASM_FUNC(DarwinCurrentEl)
  mrs x0, CurrentEL
  ret

ASM_FUNC(DarwinCurrentMpidr)
  mrs x0, mpidr_el1
  ret

ASM_FUNC(DarwinMmfr0)
  mrs x0, id_aa64mmfr0_el1
  ret

ASM_FUNC(DarwinSctlr)
  mrs x0, sctlr_el1
  ret

ASM_FUNC(DarwinTcr)
  mrs x0, tcr_el1
  ret

ASM_FUNC(DarwinPfr0)
  mrs x0, id_aa64pfr0_el1
  ret

ASM_FUNC(DarwinCacheLineSize)
  mrs x0, ctr_el0
  ubfx x0, x0, #16, #4
  mov x1, #4
  lsl x0, x1, x0
  ret

ASM_FUNC(DarwinTranslate)
  at s1e1r, x0
  isb
  mrs x0, par_el1
  ret

// x0=VA, x1=bytes. Call only on lines owned by the primary.
ASM_FUNC(DarwinCacheClean)
  cbz x1, 2f
  add x1, x0, x1
  mrs x2, ctr_el0
  ubfx x2, x2, #16, #4
  mov x3, #4
  lsl x2, x3, x2
  sub x3, x2, #1
  bic x0, x0, x3
  dsb sy
1:
  dc cvac, x0
  add x0, x0, x2
  cmp x0, x1
  b.lo 1b
  dsb sy
2:
  ret

// x0=VA, x1=bytes. The primary must have no dirty lines in this range.
ASM_FUNC(DarwinCacheInvalidate)
  cbz x1, 2f
  add x1, x0, x1
  mrs x2, ctr_el0
  ubfx x2, x2, #16, #4
  mov x3, #4
  lsl x2, x3, x2
  sub x3, x2, #1
  bic x0, x0, x3
  dsb sy
1:
  dc ivac, x0
  add x0, x0, x2
  cmp x0, x1
  b.lo 1b
  dsb sy
2:
  ret

ASM_FUNC(DarwinSendEvent)
  dsb sy
  sev
  ret

// AAPCS arguments: conduit, function, arg1, arg2, arg3.
// PSCI results are signed 32-bit values even for the SMC64 functions.
ASM_FUNC(DarwinPsciCall)
  mov x5, x0
  mov x0, x1
  mov x1, x2
  mov x2, x3
  mov x3, x4
  cmp x5, #DARWIN_PSCI_HVC
  b.eq 1f
  smc #0
  ret
1:
  hvc #0
  ret

// PSCI CPU_ON cold entry: x0=context PA, MMU off. Stackless and entirely
// self-contained in this section; no UEFI calls, globals or GIC accesses.
ASM_FUNC_ALIGN(DarwinColdEntry, 16)
  msr daifset, #15
  mov x19, x0
  ic iallu
  dsb sy
  isb
  mrs x1, mpidr_el1
  str x1, [x19, #DARWIN_CPU_MPIDR]
  mrs x1, CurrentEL
  str x1, [x19, #DARWIN_CPU_EL]
  mrs x1, id_aa64mmfr0_el1
  str x1, [x19, #DARWIN_CPU_MMFR0]
  mrs x1, sctlr_el1
  str x1, [x19, #DARWIN_CPU_SCTLR]
  dsb sy
  mov x1, #DARWIN_CPU_READY
  str x1, [x19, #DARWIN_CPU_STATE]
  dsb sy
  sev
1:
  ldr x1, [x19, #DARWIN_CPU_COMMAND]
  dsb sy
  cmp x1, #DARWIN_CPU_START
  b.eq 3f
  cbnz x1, 2f
  wfe
  b 1b
2:
  ldr x5, [x19, #DARWIN_CPU_METHOD]
  mov w0, #2
  movk w0, #0x8400, lsl #16       // PSCI_CPU_OFF
  mov x1, xzr
  mov x2, xzr
  mov x3, xzr
  cmp x5, #DARWIN_PSCI_HVC
  b.eq 4f
  smc #0
  b 5f
4:
  hvc #0
5:
  str x0, [x19, #DARWIN_CPU_OFF_RETURN]
  mov x1, #DARWIN_CPU_OFF_FAILED
  str x1, [x19, #DARWIN_CPU_STATE]
  dsb sy
  sev
6:
  wfe
  b 6b                          // CPU_OFF returned: retain code/context.
3:
  ldr x0, [x19, #DARWIN_CPU_ARGS]
  ldr x1, [x19, #DARWIN_CPU_ENTRY]
  dsb sy
  ic iallu
  dsb sy
  isb
  br x1                         // Parent verified EL1 and M/C/I off.
  .global ASM_PFX(DarwinColdEntryEnd)
ASM_PFX(DarwinColdEntryEnd):

// x0=boot_args PA, x1=entry PA, x2=owned PA, x3=owned size, x4=frame buffer
// PA (0 for none), x5=its size in bytes, x6=context PA (0 for same CPU).
// EL1 only. Migration publishes START only after the owned window is clean
// and the primary's MMU/caches are off, then parks the primary.
ASM_FUNC(DarwinEnter)
  msr daifset, #15
  mov x12, x6
  // Paint the top 1/16 of the framebuffer green (BGRA 0xFF00FF00)
  // before cache maintenance, MMU shutdown and the branch to the kernel.
  cbz x4, 2f
  lsr x9, x5, #4
  bic x9, x9, #7
  cbz x9, 2f
  mov w10, #0xFF00
  movk w10, #0xFF00, lsl #16
  mov x11, x4
3:
  stp w10, w10, [x11], #8
  subs x9, x9, #8
  b.hi 3b
  dsb sy
2:
  add x5, x2, x3
  mrs x6, ctr_el0
  ubfx x6, x6, #16, #4
  mov x7, #4
  lsl x6, x7, x6
  sub x7, x6, #1
  bic x4, x2, x7
  dsb sy
1:
  dc civac, x4
  add x4, x4, x6
  cmp x4, x5
  b.lo 1b
  dsb sy
  ic iallu
  dsb sy
  isb
  mrs x4, sctlr_el1
  bic x4, x4, #1
  bic x4, x4, #4
  bic x4, x4, #0x1000
  msr sctlr_el1, x4
  isb
  tlbi vmalle1
  dsb sy
  isb
  cbz x12, 4f
  mov x4, #DARWIN_CPU_START
  str x4, [x12, #DARWIN_CPU_COMMAND]
  dsb sy
  sev
5:
  wfe
  b 5b
4:
  br x1
