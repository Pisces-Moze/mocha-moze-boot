#!/bin/bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
out=$(realpath -m "${1:-../artifacts/uboot}")
mode=${2:-emmc}
case "$mode" in
 emmc) environment=mocha_emmc_boot;;
 ram) environment=mocha_ram_debug;;
 *) echo 'Use: build.sh [output-directory] [emmc|ram]' >&2;exit 2;;
esac
src="$out/source";mkdir -p "$out"
if [ ! -e "$src" ];then git clone --depth 1 --branch v2026.07 https://source.denx.de/u-boot/u-boot.git "$src";fi
test "$(git -C "$src" describe --tags --exact-match)" = v2026.07
cp -a "$repo/overlay/." "$src/"
cp "$repo/configs/mocha_defconfig.full" "$src/.config"
sed -i "s/^CONFIG_ENV_SOURCE_FILE=.*/CONFIG_ENV_SOURCE_FILE=\"$environment\"/" "$src/.config"
make -C "$src" CROSS_COMPILE=arm-linux-gnueabihf- olddefconfig
grep -qx "CONFIG_ENV_SOURCE_FILE=\"$environment\"" "$src/.config"
for flag in MMC_WRITE EXT4_WRITE FASTBOOT_FLASH CMD_SAVEENV CMD_GPT ENV_IS_IN_MMC;do
 if grep -qx "CONFIG_$flag=y" "$src/.config";then echo "Forbidden write feature: $flag";exit 1;fi
done
make -C "$src" CROSS_COMPILE=arm-linux-gnueabihf- -j"${JOBS:-4}" u-boot-dtb.bin
cp "$src/u-boot" "$src/u-boot-dtb.bin" "$src/.config" "$out/"
printf 'mode=%s\nenvironment=%s\nupstream_tag=v2026.07\n' "$mode" "$environment" > "$out/profile.txt"
(cd "$out";sha256sum u-boot u-boot-dtb.bin .config profile.txt > SHA256SUMS)
