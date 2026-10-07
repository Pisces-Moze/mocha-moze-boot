# Mocha Moze Boot

U-Boot2026.07的Mocha ARM32链式引导源码覆盖与构建工具。原厂Tegra bootloader/TOS/SOS保留。

## 目录与构建

```
overlay/arch/arm/dts/                 # 板级更改
overlay/board/xiaomi/mocha/           # eMMC与RAM环境
overlay/drivers/video/               # MIUI复位/配对命令/DSI quiesce
configs/mocha_defconfig.full          # 已验证引导的完整Kconfig快照
tools/build.sh                       # 固定v2026.07 + overlay
tools/pack-android.py                 # 无需原设备boot.img的legacy打包
tools/load-ram.py                     # 固定块RAM传输，不写分区
```

```sh
bash tools/build.sh ../artifacts/uboot
python3 tools/pack-android.py ../artifacts/uboot/u-boot-dtb.bin \
 ../artifacts/uboot/u-boot ../artifacts/mocha-uboot.img
fastboot boot ../artifacts/mocha-uboot.img
```

## 如何“刷入U-Boot”

它不是刷进BootROM，也没有取代最前级bootloader。Android legacy boot.img容器的kernel payload实际是u-boot-dtb.bin。
原厂bootloader加载payload到0x80a00000，U-Boot读取APP ext4中的uImage-desktop、mocha-desktop.dtb、initramfs.cpio.gz。
临时fastboot boot只在RAM运行。持久化只将这个已验证容器写入LNX（本机p22）并回读前缀SHA256，随后普通电源键开机沿同一链加载。
Android header：kernel_addr=0x10008000、ramdisk_addr=0x12000000、second_addr=0x10f00000、tags_addr=0x10000100、page2048；容器的payload ELF entry才是0x80a00000。这是已验证的Mocha原厂加载约定，不能推广到其他Tegra设备。

## 冷启动花屏修复

Fastboot路径和普通开机继承的双DSI/DC寄存器不同。只延长reset或改命令序列仍有随机半屏色偏。
最终版本在Mocha主DSIA的probe中先停止继承DC视频，关闭两路ganged DSI并等待稳定，再执行原厂毫秒级复位与双链路配对DCS初始化；不改PLL频率或供电电压。
修复后至少两次普通冷启动实机正常。Linux native交接属于另一问题，不能据此认为原生桌面完成。

## 写操作边界

禁用MMC_WRITE、EXT4_WRITE、FASTBOOT_FLASH、CMD_SAVEENV、CMD_GPT、ENV_IS_IN_MMC。env仅在RAM。
只允许eMMC读和OEM RAM命令；OEM run是调试入口，没有当成安全启动方案。将来关闭它需要另行验证安装/恢复路径。
安装与回退见[总入口](https://github.com/Pisces-Moze/mocha-moze-debian/blob/main/docs/INSTALL.md)。
