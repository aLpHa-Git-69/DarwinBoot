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

These are host and isolated compilation results. An integrated firmware/FD
build and hardware XNU boot require the consuming board's CI/device checks.
