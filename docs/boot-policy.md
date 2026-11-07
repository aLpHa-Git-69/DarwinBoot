# DarwinBoot boot policy

[简体中文](boot-policy.zh-CN.md)

DarwinBoot reads policy from the kernel's `kernel.macho.adt` sidecar. The
`UNIXNU_LOADER_PROTOCOL` and its revision remain the existing interface.
Policy is explicit; the loader does not infer a Darwin version or page size
from kernel strings.

## Typed properties

| Path | Encoding | Supported values | Omitted default |
| --- | --- | --- | --- |
| `/chosen/darwinboot-args-abi` | little-endian u32, 4 bytes | `22`, `23` | `23` |
| `/chosen/darwinboot-page-size` | little-endian u32, 4 bytes | `4096`, `16384` | `4096` |
| `/chosen/darwinboot-boot-cpu` | little-endian u64, 8 bytes | Canonical MPIDR affinity | Current CPU affinity |
| `/chosen/darwinboot-memory-size` | little-endian u64, 8 bytes | 256 MiB–8 GiB, multiples of 2 MiB | 256 MiB |
| `/psci/method` | NUL-terminated string, 4 bytes | `smc\0`, `hvc\0` | Required for a different CPU |

Canonical affinity contains only MPIDR `Aff3:Aff2:Aff1:Aff0`, mask
`0xff00ffffff`. For example, a reported raw MPIDR `0x80000100` is selected
with `darwinboot-boot-cpu = 0x100`, without MPIDR's other bits. Aff3 is supported.
The properties belong directly to `/chosen`; the PSCI method belongs directly
to the top-level `/psci` node. Duplicate properties or duplicate `/chosen` and
`/psci` nodes are rejected. A supplied PSCI method is validated even for a
same-CPU boot.

For generators supporting UniXNU's typed `[adt]` tables, a board can supply:

```toml
[adt.chosen.u32]
darwinboot-args-abi = 22
darwinboot-page-size = 16384

[adt.chosen.u64]
darwinboot-boot-cpu = 0x100
darwinboot-memory-size = 0x40000000
```

The board's hardware tree supplies `/psci/method` and CPU topology. Its boot
CPU description and the kernel configuration must match the selected affinity
and page size. These loader properties select a handoff; they do not adapt an
incompatible kernel or change the board's `/cpus` nodes.

## ABI and command line

Both structures have revision/version `2`, a shared prefix through the device
tree length, and a command line starting at `0x6c`:

| ABI | Structure bytes | Command-line bytes, including NUL | BootFlags | MemorySizeActual |
| --- | ---: | ---: | ---: | ---: |
| Darwin 22 | 736 (`0x2e0`) | 608 | `0x2d0` | `0x2d8` |
| Darwin 23 (default) | 1152 (`0x480`) | 1024 | `0x470` | `0x478` |

The loader validates ASCII and rejects more than 607/1023 command bytes. It
never silently truncates a command line. `MemorySize` and `MemorySizeActual`
both describe the requested, successfully allocated contiguous ownership
window. The arguments occupy one selected-granule page; the ADT and RAM disk
placement use that granule. Kernel/physical base alignment must support it.
Allocation still requires a real contiguous conventional-memory run and keeps
the existing size limits; there is no assumed RAM address or fallback window.

## Bootstrap bank placement

XNU's initial L2 block mappings require matching virtual/physical phases at
the **block** size, not just page alignment: 2 MiB for 4 KiB pages and 32 MiB
for 16 KiB pages. For 4 KiB, the loader retains the linked base and the
existing `ceil(descriptor start, 2 MiB) + linked base offset` allocation rule.

For 16 KiB, after parsing the kernel and ADT policy, DarwinBoot rounds the
minimum linked VA down to 32 MiB and declares that bank as `VirtualBase`.
`AllocateWindow` requires a 32 MiB-aligned physical bank from an actual
conventional-memory run. Segment, entry, argument and ADT offsets all refer
to the same bank. The prefix is zeroed along with kernel BSS and payload
storage. It is part of the requested ownership budget; the window is not
enlarged by the prefix. Bank span/VA overflow and every aligned payload reserve
are checked with the existing 2 MiB tail margin before copying or CPU startup.

For the native 8796 kernel with SHA-256
`24d6913756afe5f812c84f889c6c49c8dd3a0e08bc9177f0c183544f2a1d48b6`:

| Item | Placement with a `0xa0000000` physical bank |
| --- | --- |
| Linked kernel header VA | `0xfffffe0007004000` |
| `boot_args.VirtualBase` | `0xfffffe0006000000` |
| `boot_args.PhysicalBase` / owned size | `0xa0000000` / `0x40000000` |
| Copied header PA / entry PA | `0xa1004000` / `0xa14ba488` |
| Actual linked segment end | `0xfffffe0008984000` |
| Arguments PA / ADT PA | `0xa2984000` / `0xa2988000` |

These addresses are calculated from the actual Mach-O, not loader constants.
In particular, `0xa2934000` is inside this kernel's final segment and cannot
be used for arguments. The probe's `0xa0004000..0xfeac0000` was an **aligned
analysis candidate**, not a raw descriptor boundary. A conventional run
starting at `0xa0000000` permits the bank above. If the first free descriptor
really starts at `0xa0004000`, the first possible 32 MiB bank is `0xa2000000`;
the header/entry become `0xa3004000`/`0xa34ba488`, with arguments/ADT at
`0xa4984000`/`0xa4988000`. Allocation still needs the full requested window
after that aligned start.

## CPU lifecycle

For the current CPU, no migration context or PSCI call is needed. The loader
checks EL1 and `ID_AA64MMFR0_EL1` on that CPU, then uses the existing direct
kernel branch after cache maintenance and MMU/cache shutdown.

For a different CPU:

1. Require the requested target to be OFF according to PSCI
   `AFFINITY_INFO` (64-bit function, affinity level 0).
2. Verify that the loaded driver image and the two allocated context pages
   have physical/identity mappings. Reserve two **UEFI 4 KiB pages**, independent
   of XNU's granule, outside the kernel's allocated window. Clean the loaded
   image and initialized context to the point of coherency before `CPU_ON`.
3. Start the stackless cold-entry trampoline in the loaded image. Page 0 holds
   immutable boot-args/entry/conduit inputs and a target-owned feature report
   at offset `0x800`; page 1 holds the primary-owned WAIT/START/ABORT gate.
   The primary invalidates only target report lines while polling and cleans
   only its command page when aborting, avoiding stale-cache writeback over
   target stores.
4. The target masks interrupts, records MPIDR, CurrentEL, MMFR0 and SCTLR,
   publishes READY with barriers, and waits with `WFE`. It makes no UEFI calls
   and does not program a GIC. The primary checks affinity, EL1, M/C/I off and
   the selected granule while firmware remains active. Defined TGran4 values
   `0`/`1` and TGran16 values `1`/`2` are accepted; reserved encodings are refused.
   A migrating origin need not support the target kernel's granule.
5. After successful `ExitBootServices`, `DarwinEnter` masks interrupts, paints
   the green handoff bar, cleans the owned kernel window, and disables M/C/I.
   It then stores START physically, executes `dsb`/`sev`, and parks the origin.
   The target reads `x0 = boot_args PA`, `x1 = kernel entry PA` and branches
   with its MMU off. Native XNU owns redistributor discovery/wake and IRQ setup.

Before exit, any failure after a `CPU_ON` attempt publishes ABORT, including
an unexpected error return from `CPU_ON`. The target invokes PSCI `CPU_OFF`
using the selected conduit. The primary waits for `AFFINITY_INFO == OFF`
before freeing context pages. Preparation and active-firmware cleanup each
poll up to 2000 times with a 1 ms firmware stall. If OFF cannot be confirmed,
the reserved pages remain allocated, the driver refuses unload and another
boot is blocked until reset. A returned `CPU_OFF` result is retained for
diagnosis. A page-free failure also keeps the context resident.

An unsuccessful `ExitBootServices` call may partially shut down firmware.
The loader retries stale map keys; on a later error/exhausted retries it
quiesces the target with cache operations and PSCI only, retains the pages,
and parks. It does not resume the firmware menu in that state.

## Build and verification

The build inputs are `DarwinBootPkg/Driver/DarwinBoot.inf`, its C/assembly
sources, MdePkg and UniXNUPkg headers, plus the consuming firmware DSC/FDF.
Build integrated firmware from the resolved UniXNU tree with
`./unixnu build firmware --uefi <board-uefi-checkout>`.

In the Lavender consuming workspace, run:

```sh
python3 tests/darwin-checks.py --sanitize \
  --kernel out/ios16-kernel/kernel.development.lavender \
  --kernel-sha256 24d6913756afe5f812c84f889c6c49c8dd3a0e08bc9177f0c183544f2a1d48b6 \
  --output /data/data/com.termux/files/usr/tmp/opencode/darwinboot-checks
```

The host harness compiles the complete production loader and CPU-management
C against real MdePkg headers. It checks exact ABI bytes and sentinel tails,
command bounds, all granule encodings, actual ADT parsing, real allocator
geometry, direct/migrating success, failure cleanup, unload guards and exit
retries. Its cache mock separates target physical stores from primary cached
reports and command publication. The runner also builds ARM64 COFF C objects,
audits the real cold-entry section for relocations/stack/global references,
links the assembly into a native PE test image, checks ELF assembly and runs
the existing parser/RAM-disk/window tests. Command JSON, source hashes,
disassembly and a scope-labelled report are saved with `--output`.

With `--kernel`, the complete production loader runs on the supplied Mach-O
through mock firmware boundaries at both physical banks above. The test
poisons RAM, verifies every copied segment and zeroed prefix/BSS, and decodes
the bootstrap block descriptors independently to check segment/argument/ADT
translations. It also exercises prefix admission, tight payload bounds,
arithmetic overflow, real descriptor starts and allocation failures. Omitting
`--kernel` runs the synthetic ABI/state/bank suite without Apple input files.

These are host and isolated compilation results. An integrated firmware/FD
build and hardware XNU boot require the consuming board's CI/device checks.
