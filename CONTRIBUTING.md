# 协作方式

通用规则（证据要求、断言口径、不要提交的东西、许可与来源）见总入口的 [CONTRIBUTING.md](https://github.com/Pisces-Moze/mocha-moze-debian/blob/main/CONTRIBUTING.md)。这里只写本仓库特有的验收口径。

## 本仓库的验收口径

- 改 `overlay/drivers/video/**` 或面板初始化顺序时，附冷启动次数与结果。这类问题的现象是随机的，一次成功说明不了问题，至少连续两次普通冷启动。
- 改打包脚本时，把 `mocha-uboot.json` 的 `bytes`、`sha256`、`entry`、`page` 贴出来，并说明是在哪台设备上跑的 `fastboot boot`。
- 声明「U-Boot 能引导 Debian」时，写清是临时 `fastboot boot` 还是 LNX 持久化，并附 `uname -r` 与四核是否上线。
- 本仓库不放写分区的路径。U-Boot 侧的写功能在编译期就关掉了（`MMC_WRITE`、`EXT4_WRITE`、`FASTBOOT_FLASH`、`CMD_SAVEENV`、`CMD_GPT`、`ENV_IS_IN_MMC`），也请不要在 `tools/` 里加 flash 命令，写操作由总入口的 RAM 安装器完成。
- 改 DTS 时说明针对哪块面板、哪个批次。本机面板是 Sharp LQ079L1SX01，双 DSI 的地址与链路约定只在 Mocha 上验证过。

## 提交前自查

- `bash tools/build.sh` 能过。它有两道断言：上游 tag 必须正好是 `v2026.07`，六个写操作开关必须都未开启。
- 没有把 `.img`、`.dtb` 或本机构建产物带进提交。
- 改过 `configs/mocha_defconfig.full` 的话，`tools/build.sh` 里的断言列表仍然和它一致。
- 写清这次改动是在 RAM 里临时验证，还是已经写进 LNX。
