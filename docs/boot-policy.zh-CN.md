# DarwinBoot 启动策略

[English](boot-policy.md)

DarwinBoot 从内核旁的 `kernel.macho.adt` 读取策略，继续使用现有
`UNIXNU_LOADER_PROTOCOL` 及其协议版本。ABI 和页大小由属性明确选择，
不会根据内核中的版本字符串自动推断。

## 类型化属性

| 路径 | 编码 | 支持值 | 省略时的默认值 |
| --- | --- | --- | --- |
| `/chosen/darwinboot-args-abi` | 小端 u32，4 字节 | `22`、`23` | `23` |
| `/chosen/darwinboot-page-size` | 小端 u32，4 字节 | `4096`、`16384` | `4096` |
| `/chosen/darwinboot-boot-cpu` | 小端 u64，8 字节 | 规范化 MPIDR affinity | 当前 CPU affinity |
| `/chosen/darwinboot-memory-size` | 小端 u64，8 字节 | 256 MiB～8 GiB，2 MiB 的整数倍 | 256 MiB |
| `/psci/method` | NUL 结尾字符串，4 字节 | `smc\0`、`hvc\0` | 迁移到其他 CPU 时必需 |

规范化 affinity 只保留 MPIDR 的 `Aff3:Aff2:Aff1:Aff0`，掩码为
`0xff00ffffff`，支持 Aff3。例如，目标报告原始 MPIDR `0x80000100`
时，应传入 `darwinboot-boot-cpu = 0x100`，不能携带 MPIDR 的其他位。
启动策略属性必须直接位于 `/chosen`，PSCI 方法必须直接位于顶层
`/psci`。重复属性、重复 `/chosen` 或 `/psci` 节点均被拒绝。
同 CPU 启动时，如果提供了 PSCI 方法，也会检查其格式和值。

支持 UniXNU 类型化 `[adt]` 表的生成器可使用以下板级输入：

```toml
[adt.chosen.u32]
darwinboot-args-abi = 22
darwinboot-page-size = 16384

[adt.chosen.u64]
darwinboot-boot-cpu = 0x100
darwinboot-memory-size = 0x40000000
```

板级硬件树提供 `/psci/method` 和 CPU 拓扑。内核配置、启动 CPU 描述
必须与所选 affinity 和页大小一致。这些加载器属性只选择交接方式，
不会修改 `/cpus` 节点或适配不兼容的内核。

## ABI 与命令行

两种结构的 revision/version 均为 `2`，设备树长度之前的前缀相同，
命令行都从 `0x6c` 开始：

| ABI | 结构字节数 | 命令行容量（含 NUL） | BootFlags | MemorySizeActual |
| --- | ---: | ---: | ---: | ---: |
| Darwin 22 | 736（`0x2e0`） | 608 | `0x2d0` | `0x2d8` |
| Darwin 23（默认） | 1152（`0x480`） | 1024 | `0x470` | `0x478` |

命令行必须为 ASCII，最多分别为 607/1023 字节，不会静默截断。
`MemorySize` 和 `MemorySizeActual` 都表示实际成功分配的连续内存窗口。
参数占用一个所选页粒度的页面，ADT 和 RAM disk 按该粒度放置；
内核及物理基址必须满足对齐要求。分配仍依赖真实连续的
`EfiConventionalMemory` 区间及原有大小限制，没有预设 RAM 地址或回退窗口。

## Bootstrap bank 放置

XNU 的初始 L2 block 映射要求 VA/PA 在 **block 大小** 上的相位一致，
仅满足页对齐不够。4 KiB 页对应 2 MiB block，16 KiB 页对应 32 MiB block。
4 KiB 路径保留链接基址，以及原有的“描述符起点向上对齐到 2 MiB，
再加链接基址偏移”的分配规则。

选择 16 KiB 时，解析内核和 ADT 策略后，DarwinBoot 将最低链接 VA
向下对齐到 32 MiB，并将此 bank 写入 `VirtualBase`。`AllocateWindow`
从真实连续的 conventional-memory 区间分配 32 MiB 对齐的物理 bank。
内核段、入口、参数和 ADT 都使用相对于同一 bank 的偏移。前缀与内核
BSS、负载区域一起清零，占用原有窗口预算，不额外扩大窗口。复制或
启动目标 CPU 前，检查 bank 跨度、VA 溢出及每项负载的对齐/容量，
保留原有 2 MiB 尾部余量。

原生 8796 内核的 SHA-256 为
`24d6913756afe5f812c84f889c6c49c8dd3a0e08bc9177f0c183544f2a1d48b6` 时：

| 项目 | 物理 bank 为 `0xa0000000` 时的地址 |
| --- | --- |
| 内核头链接 VA | `0xfffffe0007004000` |
| `boot_args.VirtualBase` | `0xfffffe0006000000` |
| `boot_args.PhysicalBase` / 窗口大小 | `0xa0000000` / `0x40000000` |
| 复制后的内核头 PA / 入口 PA | `0xa1004000` / `0xa14ba488` |
| 实际链接段末尾 | `0xfffffe0008984000` |
| 参数 PA / ADT PA | `0xa2984000` / `0xa2988000` |

这些地址由实际 Mach-O 计算，不是加载器常量。`0xa2934000` 位于该内核
最后一个段内，不能用来放置参数。探针报告的 `0xa0004000..0xfeac0000`
是 **经过对齐分析的候选范围**，并非原始描述符边界。起于 `0xa0000000`
的 conventional 区间允许上述 bank；若第一个空闲描述符实际起于
`0xa0004000`，第一个可用 32 MiB bank 是 `0xa2000000`，内核头/入口
变为 `0xa3004000`/`0xa34ba488`，参数/ADT 变为 `0xa4984000`/`0xa4988000`。
对齐后的起点之后仍必须容纳完整请求窗口。

## CPU 生命周期

选择当前 CPU 时，不分配迁移上下文，也不调用 PSCI。加载器检查该
CPU 的 EL1 和 MMFR0 页粒度能力，然后沿用缓存维护、关闭 MMU/缓存、
直接跳入内核的路径。

选择其他 CPU 时：

1. 用 PSCI `AFFINITY_INFO` 的 64 位接口、affinity level 0，确认目标
   初始状态为 OFF。
2. 检查已加载驱动镜像及两个上下文页面的物理/恒等映射。上下文使用
   两个 **UEFI 4 KiB 页面**，与 XNU 的页粒度独立，位于内核窗口之外。
   `CPU_ON` 前将驱动镜像和初始化后的上下文清理到一致性点。
3. 从驱动镜像中的无栈冷入口启动目标。第 0 页存放不可变的参数地址、
   内核入口、PSCI 方法，以及从 `0x800` 开始的目标独占特征报告；
   第 1 页是主 CPU 独占写入的 WAIT/START/ABORT 命令门。主 CPU 轮询时
   只失效报告缓存行，取消时只清理自己的命令页，避免旧缓存回写覆盖
   目标的物理写入。
4. 目标屏蔽中断，记录 MPIDR、CurrentEL、MMFR0、SCTLR，通过屏障发布
   READY，然后用 `WFE` 等待。它不调用 UEFI，也不编程 GIC。主 CPU 在
   固件仍活动时确认目标 affinity、EL1、M/C/I 已关闭及所选页粒度。
   TGran4 的 `0`/`1`、TGran16 的 `1`/`2` 编码被接受，保留编码被拒绝。
   迁移时，原 CPU 不必支持目标内核的页粒度。
5. `ExitBootServices` 成功后，`DarwinEnter` 屏蔽中断、绘制顶部绿条、
   清理拥有的内核窗口并关闭 M/C/I，然后以物理访问写入 START，执行
   `dsb`/`sev` 并停驻原 CPU。目标读取 `x0 = boot_args 物理地址`、
   `x1 = 内核入口物理地址`，在 MMU 关闭状态下跳转。原生 XNU 负责
   redistributor 的发现、唤醒及 IRQ 设置。

退出前，任何发生在 `CPU_ON` 尝试之后的失败（包括意外的错误返回）
都会发布 ABORT。目标按所选方法调用 PSCI `CPU_OFF`，主 CPU 只有在
`AFFINITY_INFO == OFF` 后才释放上下文。准备及固件活动时的取消流程
各最多轮询 2000 次，每次使用 1 ms 固件 stall。无法确认 OFF 时保留
两个页面和驱动镜像，拒绝卸载及后续启动，直到复位；返回的
`CPU_OFF` 结果保留用于诊断。页面释放失败也会保留上下文。

失败的 `ExitBootServices` 调用可能已经部分关闭固件。加载器会重试
失效的 map key；后续错误或重试耗尽时，仅用缓存操作及 PSCI 取消
目标、保留页面并停驻，不会在此状态返回固件菜单。

## 构建与验证

构建入口为 `DarwinBootPkg/Driver/DarwinBoot.inf`，依赖其 C/汇编源码、
MdePkg、UniXNUPkg 头文件及消费组件的 DSC/FDF。在已解析的 UniXNU
构建树中用 `./unixnu build firmware --uefi <板级 UEFI 仓库>` 构建固件。

在 Lavender 消费工作区运行：

```sh
python3 tests/darwin-checks.py --sanitize \
  --kernel out/ios16-kernel/kernel.development.lavender \
  --kernel-sha256 24d6913756afe5f812c84f889c6c49c8dd3a0e08bc9177f0c183544f2a1d48b6 \
  --output /data/data/com.termux/files/usr/tmp/opencode/darwinboot-checks
```

主机测试使用真实 MdePkg 头文件编译完整加载器及 CPU 管理 C 源码，
检查 ABI 精确字节、尾部哨兵、命令行边界、全部页粒度编码、实际 ADT
解析器和分配器、同 CPU/迁移成功路径、错误清理、卸载保护及退出重试。
缓存 mock 将目标物理写入与主 CPU 缓存报告、命令发布分开建模。
脚本还交叉编译 ARM64 COFF C 对象，检查实际冷入口的重定位、栈和
全局引用，将汇编链接为原生 PE 测试镜像，验证 ELF 汇编并运行现有
解析器、RAM disk 和窗口测试。指定 `--output` 后保存命令 JSON、
源码哈希、反汇编和标明验证范围的报告。

指定 `--kernel` 后，完整生产加载器通过 mock 固件边界，使用输入
Mach-O 在上述两个物理 bank 上运行。测试先填充非零 RAM，检查所有
段的复制、前缀/BSS 清零，并独立解码 bootstrap block 描述符，验证
段、参数和 ADT 的地址转换；同时覆盖前缀预算、紧窗口、算术溢出、
真实描述符起点和分配失败。省略该选项时运行不需要 Apple 输入文件的
合成 ABI/状态/bank 测试。

以上属于主机及独立编译检查。完整固件/FD 构建和硬件 XNU 启动需要
消费板级组件的 CI 与设备验证。
