# DarwinBoot

UEFI XNU loader with packed RAM disk handoff support.

The Mach-O parser accepts arm64 and arm64e MH_EXECUTE kernels, including
the capability bits in the arm64e CPU subtype. XNU initializes pointer
authentication and applies its own chained fixups. MH_FILESET input is
still unsupported by this loader.

UniXNU passes the selected boot entry's arguments to DarwinBoot, which copies
them into XNU's command line and prints `DARWINBOOT: command line:` before
handoff. The top green bar marks `DarwinEnter` before cache maintenance,
MMU shutdown and the branch to XNU. Follow the
[boot argument workflow](https://unixnu.com/xnu/boot-arguments#apply-arguments)
and [green-bar diagnosis](https://unixnu.com/guide/debugging#green-bar).

Build and interface details: [documentation](https://unixnu.com/reference/components).
Sources and dependencies: [catalog](https://unixnu.com/reference/components).

## Optional boot policy

The default handoff uses Darwin 23's 1,152-byte `boot_args`, a 1,024-byte
command-line field, 4 KiB pages and the current CPU. The ADT sidecar can select
Darwin 22's 736-byte ABI (608-byte command-line field), 16 KiB pages and a
different canonical MPIDR affinity through typed `/chosen` properties:

| Property | ADT type | Values / default |
| --- | --- | --- |
| `darwinboot-args-abi` | little-endian u32 | `22` or `23`; default `23` |
| `darwinboot-page-size` | little-endian u32 | `4096` or `16384`; default `4096` |
| `darwinboot-boot-cpu` | little-endian u64 | MPIDR affinity only; default current CPU |
| `darwinboot-memory-size` | little-endian u64 | 256 MiB–8 GiB in 2 MiB steps; default 256 MiB |

A different CPU requires `/psci/method` to be exactly `smc\0` or `hvc\0`.
DarwinBoot prepares an initially OFF target using PSCI while firmware is
active, verifies its affinity, EL1 and page-granule support, then releases it
after successful `ExitBootServices`. The target uses a stackless cold entry;
XNU owns interrupt-controller initialization. Failure sends ABORT and frees
the two context pages only after PSCI confirms OFF. An unquiesced target keeps
the context and driver image resident and blocks another boot/unload.

Command lines include a terminating NUL: the limits are 607 ASCII bytes for
Darwin 22 and 1,023 for Darwin 23. Oversized lines, malformed/duplicate policy
properties and unsupported values are refused before `ExitBootServices`.
The selected granule is checked on the CPU that will enter XNU.

For 16 KiB pages, XNU's bootstrap L2 blocks are 32 MiB. DarwinBoot rounds the
linked virtual base down to a 32 MiB bank, allocates a 32 MiB-aligned physical
bank, and copies each segment at its linked offset within that bank. The
zeroed prefix counts against the requested window. Arguments and ADT follow
the actual segment end; this keeps their VA/PA block phases coherent. The
4 KiB path retains its linked base and existing 2 MiB allocation rule. See
[bootstrap bank placement](docs/boot-policy.md#bootstrap-bank-placement).

Full interface, lifecycle and verification details:
[English](docs/boot-policy.md) · [简体中文](docs/boot-policy.zh-CN.md).
Host and isolated ARM64/COFF/PE checks are available at the consuming workspace's
`tests/darwin-checks.py`; firmware integration and device kernel boot are separate
checks.

The optional `kernel.macho.vroot` sidecar carries up to 128 MiB of signed root
policies. The loader checks its envelope and supplies its physical range as
`/chosen/memory-map/RootPolicies`. XNU authenticates the policies using an owner
anchor compiled into the source storage runtime. This handoff is independent
of the Data RAM disk, GPU addon and Apple trust cache. The new handoff has not
yet been built or exercised.
