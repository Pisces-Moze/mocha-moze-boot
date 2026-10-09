# Mocha Moze Boot

> U-Boot 2026.07 的 Mocha 板级覆盖、Android legacy 容器打包，以及只在 RAM 里运行的加载工具。

[![License: GPL-2.0-only](https://img.shields.io/badge/License-GPL--2.0--only-blue.svg)](LICENSE)

## 简介

这个仓库负责 Mocha 引导链里 U-Boot 这一段：把上游 U-Boot 2026.07 编成能在该机型上接手的引导器，按原厂约定打包成 Android legacy `boot.img` 容器，再从 eMMC 的 APP 分区读出内核、设备树与 initramfs 交给 Linux。原厂 Tegra bootloader、TOS 与 SOS 全部保留，本仓库不替换它们，也不改动 BootROM。

它本身不产生可安装的系统。发行版 rootfs 与安装、回退流程在 [mocha-moze-debian](https://github.com/Pisces-Moze/mocha-moze-debian)，6.12.111 内核在 [mocha-moze-linux](https://github.com/Pisces-Moze/mocha-moze-linux)，背光、音频与 GPU 实验在 [mocha-moze-drivers](https://github.com/Pisces-Moze/mocha-moze-drivers)，Niri、Noctalia 与充电界面在 [mocha-moze-desktop](https://github.com/Pisces-Moze/mocha-moze-desktop)。本仓库交付的是一份 `mocha-uboot.img` 和一条只跑在 RAM 里的调试通道。

## 引导链

| 阶段 | 执行者 | 做了什么 |
|---|---|---|
| 1 | BootROM | 从 eMMC 取原厂 bootloader，本仓库不参与 |
| 2 | 原厂 Tegra bootloader / TOS / SOS | 保留运行，完成 DRAM、时钟与电源初始化 |
| 3 | Android legacy `boot.img` 容器 | 容器里的 kernel payload 实际是 `u-boot-dtb.bin` |
| 4 | U-Boot 2026.07 | 从 APP ext4 读 `uImage-desktop`、`mocha-desktop.dtb`、`initramfs.cpio.gz` |
| 5 | Linux | 接手显示、USB 与外设 |

第 4 步的命令写在 `overlay/board/xiaomi/mocha/mocha_emmc_boot.env` 的 `boot_debian` 里：`mmc dev 0` 之后依次 `ext4load mmc 0#APP` 三个文件，`fdt addr 8c000000` 加 `fdt resize 10000`，再 `bootm 80007fc0 88000000:${rdsize} 8c000000`。任一环失败时 `bootcmd` 会打印 `Debian boot unavailable - USB Fastboot` 并进入 USB fastboot，不会卡在没有输出的循环里。控制台同时走 UARTD 串口（115200）和面板（`stdout=serial,vidconsole`，16×32 字体、白字黑底）。这份配置关掉数据 cache（`CONFIG_SYS_DCACHE_OFF=y`）并跳过底层初始化（`CONFIG_SKIP_LOWLEVEL_INIT=y`），DRAM 与时钟交给上游原厂 bootloader；RAM 诊断环境里那句 `data cache disabled` 指的是同一件事。

地址与尺寸的对应关系：

| 项 | 值 | 出处 |
|---|---|---|
| 容器 payload 载入地址、ELF entry | `0x80a00000` | `tools/pack-android.py:7`、`configs/mocha_defconfig.full:190,332` |
| Android header `kernel_addr` | `0x10008000` | `tools/pack-android.py:11` |
| Android header `ramdisk_addr` | `0x12000000` | 同上 |
| Android header `second_addr` | `0x10f00000` | 同上 |
| Android header `tags_addr` | `0x10000100` | 同上 |
| `page_size` | 2048 | 同上 |
| uImage 载入地址 | `0x80007fc0` | `mocha_emmc_boot.env:5` |
| uImage 头声明的 load 与 entry | `0x80008000` | `tools/load-ram.py:23` |
| initramfs 地址 | `0x88000000` | `mocha_emmc_boot.env:5` |
| DTB 地址 | `0x8c000000` | 同上 |
| fastboot 传输缓冲区 | `0x91000000`，长 `0x10000000` | `configs/mocha_defconfig.full:1134-1135` |

header 里那四个地址沿用原厂加载约定，payload 实际落在哪里由 ELF entry 决定。这一组取值只在 Mocha 上验证过，不能推广到其他 Tegra 设备。

## 目录结构

| 路径 | 作用 |
|---|---|
| `overlay/arch/arm/dts/tegra124-xiaomi-mocha.dts` | 板级设备树 |
| `overlay/board/xiaomi/mocha/mocha_emmc_boot.env` | 默认编译进 U-Boot 的环境，负责 eMMC 引导 |
| `overlay/board/xiaomi/mocha/mocha_ram_debug.env` | `build.sh <out> ram` 选用；打印自标 `MC-SMMU-config` 的 `md.l 70019010 1` 后进 fastboot |
| `overlay/drivers/video/sharp-lq079l1sx01.c` | 面板驱动：复位时序、成对 DCS 命令、时序参数 |
| `overlay/drivers/video/tegra/dsi.c` | Tegra DSI 桥驱动，含 Mocha 冷启动 quiesce |
| `configs/mocha_defconfig.full` | 已验证引导的完整 `.config` 快照，构建时直接当 `.config` 用 |
| `tools/build.sh` | 固定 v2026.07 加 overlay，构建并断言写功能未开启 |
| `tools/pack-android.py` | 生成容器，不读原设备的 `boot.img` |
| `tools/load-ram.py` | 固定块 RAM 传输，不写分区 |

`overlay/` 按上游 U-Boot 的目录形状摆放（`arch/arm/dts/`、`board/xiaomi/mocha/`、`drivers/video/`），构建时整份覆盖进源码树。`.gitignore` 排除 `*.img` 与 `*.dtb`，构建产物不随仓库提交。

设备树写的是这块板子的实际接法：2 GiB 内存（`0x80000000` 起，长 `0x80000000`）；`mmc0` 指 eMMC（sdmmc4，8 位、不可插拔），`mmc1` 指 uSD（sdmmc3，4 位、带 CD 与供电 GPIO），`usb0` 指 usb1；DSIA 用 `nvidia,ganged-mode` 拴住 DSIB，主从两个 panel 节点通过 `link2` 配对，panel 侧供电为 avdd 1.8 V、vddio 1.8 V、vsp 与 vsn 5.5 V，复位在 `TEGRA_GPIO(H, 3)`；PMIC 是 pwr_i2c 上的 TPS65913，背光是 gen1 I²C 上的 `ti,lp8556`（地址 `0x2c`，带一组 ROM 初值）；电源键映射到 `KEY_ENTER`，音量键到 `KEY_UP` 与 `KEY_DOWN`，两个霍尔传感器报 `SW_LID`。文件末尾另有一节注释为「Mocha RAM USB diagnostic variant」的节点，把 usb1 改成 `peripheral` 并给 `usb-phy@7d000000` 置 `okay` 与一组 xcvr 参数；两段写在同一份 DTS 里，编出的 DTB 里生效的就是这份 peripheral 配置。

## 部署与使用

前置条件：主机上有 `bash`、`git`、`make`、`python3` 和 `arm-linux-gnueabihf-` 交叉工具链（配置快照记录的编译器是 gcc 14.2.0，见 `configs/mocha_defconfig.full` 的 `CONFIG_GCC_VERSION=140200`）；设备能进原厂 fastboot。首次构建会从 `https://source.denx.de/u-boot/u-boot.git` 浅克隆 `v2026.07` 到输出目录下的 `source/`，并用 `git describe --tags --exact-match` 断言 tag 正好是 `v2026.07`。

### 构建

```sh
bash tools/build.sh ../artifacts/uboot
```

第一个参数是输出目录，默认 `../artifacts/uboot`；`JOBS` 控制 make 并行度，默认 4。脚本把 `overlay/` 覆盖进源码树，把 `configs/mocha_defconfig.full` 直接作为 `.config` 跑 `olddefconfig`，然后在编译前逐项检查下面六个符号，命中任意一个就打印 `Forbidden write feature: <名字>` 并非零退出：

第二个参数选择 `emmc`（默认）或 `ram`。`emmc` 保持原有 APP 自动引导环境；`ram` 使用已有的 `mocha_ram_debug.env`，启动后留在 U-Boot Fastboot，供 `load-ram.py` 传入候选内核。未知模式在下载和创建输出目录前失败；`olddefconfig` 后还检查实际选中的 `CONFIG_ENV_SOURCE_FILE`。输出的 `profile.txt` 记录模式与环境名，并纳入 `SHA256SUMS`。

```
CONFIG_MMC_WRITE  CONFIG_EXT4_WRITE  CONFIG_FASTBOOT_FLASH
CONFIG_CMD_SAVEENV  CONFIG_CMD_GPT  CONFIG_ENV_IS_IN_MMC
```

构建目标是 `u-boot-dtb.bin`；`u-boot`、`u-boot-dtb.bin` 与 `.config` 被拷到输出目录，并在那里生成 `SHA256SUMS`。

### 打包 Android legacy 容器

```sh
python3 tools/pack-android.py \
  ../artifacts/uboot/u-boot-dtb.bin \
  ../artifacts/uboot/u-boot \
  ../artifacts/mocha-uboot.img
```

三个位置参数依次是 payload、ELF 和输出。脚本自己拼容器头：`magic` 为 `ANDROID!`、`name` 为 `Mocha-Moze-UBoot`、`cmdline` 为空、`id` 按 legacy 算法用 SHA1 算（kernel 数据段加长度，再加两个空段），header 与 payload 各自补齐到 2048 的整数倍。它不读原设备的 `boot.img`，也不需要任何私有备份，头里的每个字段都是脚本里写死的已验证取值。

校验有两条：ELF 必须是 ARM32（`\x7fELF\x01\x01`、`e_machine=40`）且 entry 为 `0x80a00000`；payload 大小必须大于 128 KiB 且小于 8 MiB。产物旁边的 `mocha-uboot.json` 记下 `bytes`、`sha256`、`entry` 与 `page`，同一份元数据也打印到标准输出，回读校验用这里的 `sha256`。

### 只在 RAM 里启动

```sh
fastboot boot ../artifacts/mocha-uboot.img
```

原厂 bootloader 把容器读进 RAM，从 `0x80a00000` 开始执行，eMMC 上的内容不变；重新上电就回到改动前的状态。改 U-Boot 时先用这条路验证，[CONTRIBUTING.md](CONTRIBUTING.md) 也是这个口径：先用临时镜像验证，再讨论默认引导。

APP 已安装时，默认镜像会尝试自动进入 Debian。需要停在 U-Boot Fastboot 并用 RAM 加载器测试新内核时，构建单独的 `ram` 镜像：

```sh
bash tools/build.sh ../artifacts/uboot-ram ram
python3 tools/pack-android.py \
  ../artifacts/uboot-ram/u-boot-dtb.bin ../artifacts/uboot-ram/u-boot \
  ../artifacts/mocha-uboot-ram.img
fastboot boot ../artifacts/mocha-uboot-ram.img
```

然后核对 U-Boot 的产品与版本，再执行下面的 `load-ram.py`。2026-10-09 在 Debian 13／GCC 14.2.0 上，`emmc` 与 `ram` 两种模式均完整编译通过；实际 `.config` 的环境选择、六项禁写断言、产物 SHA256 和 RAM Android 容器头／payload／SHA1 均已检查。生成的 RAM `bootcmd` 确实进入 `fastboot usb 0`。这些是构建检查，新模式的实机 USB、RAM 内核引导和画面仍需验收，不能沿用默认镜像以前的冷启动结果。

2026-10-10 从原厂 Fastboot 临时加载新 RAM 镜像，上传与 `boot` 均返回 OKAY；用户确认屏幕正常，并看到 `Mocha RAM USB diagnostics`。Windows 枚举出新的 `USB download gadget` 实例，但它还需单独绑定驱动；U-Boot Fastboot 命令和候选 Linux 引导仍待验证。这次画面观察只覆盖 RAM U-Boot，不代表 native Linux 桌面或默认冷启动通过。

### 写入 LNX 并验证

持久化写入的做法是：把已经验证过的这份容器写进 LNX 分区（本机设备上为 p22），回读该分区前缀的 SHA256，与 `mocha-uboot.json` 里的 `sha256` 对照，再断电重开，确认普通电源键开机能沿同一条链走到 Linux。

本仓库没有这一步的脚本。U-Boot 侧的写功能在编译期就关掉了（`FASTBOOT_FLASH`、`MMC_WRITE`、`EXT4_WRITE` 等），写入动作发生在 U-Boot 之外，由总入口的 `tools/storage.sh write LNX <img> <layout.env> REPLACE_LNX` 在只读 RAM 环境里执行，逐条命令见 [INSTALL.md 第 8 节](https://github.com/Pisces-Moze/mocha-moze-debian/blob/main/docs/INSTALL.md)。该脚本按 `layout.env` 校验设备 CID、磁盘容量、分区起点与扇区数，只写镜像长度、回读前缀 SHA256 比对，失败即中止，结束时把磁盘和分区设回只读。这一步会改动 eMMC 上的 LNX 分区，所以先跑通上一条命令、看清屏幕与 USB 的实际表现，再考虑持久化。

### 从 RAM 直接引导 Linux

设备已经跑着 Mocha U-Boot 2026.07 时，`tools/load-ram.py` 可以不写任何分区就把内核送进内存：

```sh
python3 tools/load-ram.py --serial <设备序列号> \
  --kernel uImage-desktop \
  --initrd initramfs.cpio.gz \
  --dtb mocha-desktop.dtb
```

它逐条下发 `oem run:`：每 1 MiB 先 `stage` 到 fastboot 缓冲区 `0x91000000`，再 `cp.b` 到目标地址，kernel 落 `0x80007fc0`、initrd 落 `0x88000000`、dtb 落 `0x8c000000`，并检查三段区间不越界（上界分别是 `0x88000000`、`0x8c000000`、`0x8d000000`）。开工前先取 `getvar version-bootloader` 与 `getvar product`，字符串里必须含 `U-Boot`、`2026.07` 和 `mocha`，否则直接退出；uImage 必须是未压缩的 ARM Linux 镜像（magic `0x27051956`、load 与 entry 均为 `0x80008000`、OS/arch/type/comp 为 5/2/2/0、头 CRC 与数据 CRC 都对得上）；单条 `oem run:` 超过 64 字节会被拒绝。最后设 `fdt_high` 与 `initrd_high` 为 `ffffffff`，拼出 bootargs，`fdt addr 8c000000` 加 `fdt resize 10000`，再 `bootm 80007fc0 88000000:<initrd 长度> 8c000000`。下发这条命令时 USB 会断开，脚本按正常情况处理这次超时。它只写 RAM，脚本最后一行提醒：在任何存储写之前，先看实际屏幕与 USB。

## 如何刷入 U-Boot

三处容易弄错。

BootROM 不参与。它照旧从 eMMC 取原厂 bootloader，本仓库不碰这一段，也没有任何绕过签名校验的改动。

最前级 bootloader 也不在替换范围内。U-Boot 在这里的身份是一份被原厂 bootloader 当作 Android `boot.img` 加载的 payload：容器头写的是原厂约定，payload 落点由 ELF entry `0x80a00000` 决定。header 字段与 2048 的页面大小都只在 Mocha 上验证过。

`fastboot boot` 与写入 LNX 是两条不同的路径。前者每次把容器从 USB 送进 RAM，断电即消失；后者把同一份容器放到存储上，让普通电源键开机也沿这条链加载。写入只针对这一个已验证的容器，不碰别的分区。

## 冷启动花屏

原厂正常开机和 `fastboot boot` 两条路径下，DSIA 与 DC 继承到的寄存器状态不同，前者会把两路 DSI 留在出图状态。只把复位拉长，或者调整命令序列，仍会随机出现半屏色偏；随机性来自继承状态，单靠延时参数调不出来。

最终做法落在 `overlay/drivers/video/tegra/dsi.c` 的 `mocha_dsi_quiesce_inherited()`，它只对 `xiaomi,mocha` 且只在主 DSIA（`0x54300000`）实例上执行，位置在 ganged 探测和 DSI 复位之前：

| 步骤 | 操作 |
|---|---|
| 1 | 打开 `PERIPH_ID_DISP1`、`PERIPH_ID_DSI`、`PERIPH_ID_DSIB` 时钟门，`udelay(2)` |
| 2 | 清 DC（`0x54200000`）`disp_cmd` 的 `CTRL_MODE_MASK`，`disp_win_opt` 写 0，置 `GENERAL_UPDATE` 与 `GENERAL_ACT_REQ` |
| 3 | `mdelay(34)`：按注释留两个 60 Hz 帧供继承的流出图停止 |
| 4 | 对 `0x54300000` 与 `0x54400000` 两路，`ganged_mode_ctrl` 写 0、清 `DSI_POWER_CONTROL_ENABLE`、`int_enable` 与 `dsi_trigger` 写 0 |
| 5 | `mdelay(5)` 后回到 probe 流程 |

之后是原厂的毫秒级复位与双链路成对初始化。DSI probe 的顺序是 `reset_assert`、使能 avdd、配时钟、`mdelay(2)`、`reset_deassert`；面板驱动按 vddio、avdd、等 12 ms、vsp、等 12 ms、vsn、等 24 ms、复位拉低 2 ms、拉高 2 ms、拉低 32 ms 上电；DCS 命令经 `sharp_lq079l1sx01_write_both()` 同时下给两条链路，睡眠退出后等 120 ms，再依次写亮度 `0xff`、省电 `0x01`、显示控制 `0x2c` 和 display on，最后等 150 ms。这段路径只改时钟门，PLL 频率与供电轨电压不动（代码注释原话：changes clock gates, never PLL rates or rail voltages）。面板时序为 `pixelclock 215000000`、`1536×2048`（hfront 136、hback 28、hsync 28、vfront 14、vback 8、vsync 2），4 lane、RGB888、video 加 LPM 模式。

验证到哪一步：修复后至少两次普通冷启动在实机上正常出图。按 [CONTRIBUTING.md](CONTRIBUTING.md) 的口径，编译通过不算实机通过，DRM page flip 成功也不算面板有图，所以这里的结论以实机屏幕观察为准。Linux native 交接属于另一个问题，冷启动正常不能推出原生桌面已经完成。

## 写操作边界

| 关掉的符号 | 结果 |
|---|---|
| `CONFIG_MMC_WRITE` | `mmc write` 不进镜像 |
| `CONFIG_EXT4_WRITE`、`CONFIG_CMD_EXT4_WRITE` | 文件系统层与命令层的 ext4 写都没有 |
| `CONFIG_FASTBOOT_FLASH` | USB fastboot 不能落盘 |
| `CONFIG_CMD_SAVEENV` | 没有 `saveenv` |
| `CONFIG_CMD_GPT` | 不能在 U-Boot 里改分区表 |
| `CONFIG_ENV_IS_IN_MMC` | 环境不落在 eMMC 上 |

配合 `CONFIG_ENV_IS_NOWHERE=y` 与 `CONFIG_ENV_IS_DEFAULT=y`，环境只在 RAM 里从编译进去的默认值建起（源文件 `mocha_emmc_boot`，`CONFIG_ENV_SIZE=0x3000`）。留下的读写面是 eMMC 只读（`mmc dev` 加 `ext4load` 读 APP 分区）与 OEM RAM 命令（`CONFIG_FASTBOOT_OEM_RUN`、`CONFIG_FASTBOOT_CMD_OEM_CONSOLE`）。

后果是引导器自己改不动 eMMC 的内容、分区表和环境变量，持久化动作必须发生在 U-Boot 之外，本仓库因此不提供写入路径。OEM run 把 U-Boot 的命令入口开在 USB 上，调试时方便，但它没有被当成安全启动方案；将来要关掉它，得先另行验证安装与恢复路径。当前 `CONFIG_FASTBOOT_OEM_RUN=y` 是刻意保留的调试入口。

没收口的地方：`CONFIG_FAT_WRITE=y`、`CONFIG_FAT_RENAME=y`、`CONFIG_CMD_SF=y`、`CONFIG_SPI_FLASH_UNLOCK_ALL=y` 仍在配置里，FAT 写入与 SPI flash 擦写命令都还可用，而 `tools/build.sh` 的断言只覆盖上面那张表的六个符号。这些介质上有没有可写区域，未确认。

## 未实现与计划

| 条目 | 现状 | 出处 |
|---|---|---|
| ganged 非对称切分 | 只支持左右对称切分，代码内留着 TODO | `overlay/drivers/video/tegra/dsi.c:796` |
| 关闭 OEM run | 现为调试入口，关闭前需另验安装与恢复路径 | `configs/mocha_defconfig.full:1139`（当前为 `y`） |
| 持久化写入工具 | 仓库内没有脚本，`tools/` 只有三份 | `tools/` |
| RAM 诊断环境的启用 | 构建脚本已接通 `ram` 模式；新模式的实机 USB 与候选内核引导待验收 | `tools/build.sh`、`overlay/board/xiaomi/mocha/mocha_ram_debug.env` |
| FAT 写入与 SPI flash 命令 | 仍在编译里，六项断言覆盖不到 | `configs/mocha_defconfig.full:759,1356,1946-1947` |
| SPL | 配置里有 SPL 相关符号，`build.sh` 只构建 `u-boot-dtb.bin` | `configs/mocha_defconfig.full:215,225`、`tools/build.sh:14` |
| 设备树 overlay | 未启用 | `configs/mocha_defconfig.full:206` |
| Linux native 交接与原生桌面 | 属于 mocha-moze-linux 与 mocha-moze-desktop | 兄弟仓库 |
| CPU/GPU 调频与超频 | 保持未启用 | `CONTRIBUTING.md:6` |
| 冷启动实机验证 | 记录为至少两次普通冷启动正常，没有更系统的批量验证 | `overlay/drivers/video/tegra/dsi.c:1016` |

## 许可证与来源

`LICENSE` 是 GPL-2.0 全文，开头附一份 SPDX 标识写法说明（`GPL-2.0`、`GPL-2.0-only`、`GPL-2.0+`、`GPL-2.0-or-later`）。`LICENSE-NOTES.md` 划的界是：Linux、U-Boot 与厂商 GPL 文件保留各自的 SPDX、版权头和原许可证；本项目新写的内核与驱动适配、工具和文档都用 GPL-2.0-only；Niri/Smithay、Noctalia、Gdev 等外部项目沿用上游许可，补丁不改变上游许可证，参考源码出处见总入口的 SOURCES.md。

上游许可的影响是具体的：`overlay/drivers/video/tegra/dsi.c` 与 `overlay/drivers/video/sharp-lq079l1sx01.c` 都标 `SPDX-License-Identifier: GPL-2.0+`，版权头分别归 NVIDIA 与 Svyatoslav Ryhel；`tools/build.sh` 从 `source.denx.de` 拉 U-Boot `v2026.07` 再叠加覆盖，重新分发这段代码或它的二进制时，GPL-2.0 的源码提供义务跟着走。

仓库不授予 NVIDIA CUDA、MIUI 固件、Wi-Fi/蓝牙固件或 TFA DSP 参数的再分发权，开发者自行从合法持有的设备或官方包提取。

协作与提交要求见 [CONTRIBUTING.md](CONTRIBUTING.md)：提交要写明仓库 commit、内核 release、DT SHA256、冷启动还是 Fastboot 临时启动、实际屏幕观察与系统日志；设备密钥、Wi-Fi 密码、完整存储镜像、MAC 地址和未经许可的固件不要提交。
