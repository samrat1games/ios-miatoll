# miatoll bring-up checklist

Everything in `board/miatoll.json` with `"verified": false` is a copy of sweet and must be checked against the miatoll stock DTB.

1. Pull stock `boot.img` (recovery/adb, nothing flashed), `python3 tools/unpack_bootv2.py boot.img dtb/boot_unpacked`, `dtc -I dtb -O dts` it.
2. Fill the JSON: memory nodes + reserved-memory (`cont_splash_region`), GIC (`intc@17a00000`), `ufshc`/`sdhci` (UFS or eMMC!), `pshold`, `qcom,msm-imem` restart_reason, cpu MPIDRs, timer.
3. Storage: if eMMC, `shim/ufs.c` and the XNU UFS patches (0028-0034) need an SDHCI/eMMC replacement.
4. Touch: identify the panel IC (Goodix/FocalTech/Novatek); patches 0060/0063 are GT9896-specific.
5. Buttons/PMIC: SPMI SIDs and GPIOs for power/volume (patch 0060); regulator rails (0062).
6. CPU: patches 0065/0078 assume sweet's MPIDR layout.
7. Build kernel via CI (fork needed), then QEMU smoke test: `tools/qemu-sweet.py`.

## Status 2026-10-20 (curtana / Redmi Note 9S, FT8756 TDDI)

- Board values verified from the live FDT (see `board/miatoll.json`).
- Touch: `xnu/patches/0082-miatoll-ft8756-spi-touch.patch` adds a read-only FT8756 SPI driver
  (GPIO bit-bang on GPIO53-56, vendor protocol from MiCode curtana-q-oss), boot-arg `sweettouchspi=0` falls back to the I2C path.
  Compile-checked with stubs only; NOT tested on hardware. No reset pulse is sent (TDDI: touch reset = display reset).
  Unverified: pin roles (probe tries MISO/MOSI swap), L18A rail via RPMh resource "ldoa18", whether the touch FW is running when ABL hands over.
- Buttons (power/volume via PM6150 SPMI, patch 0060 `sweet_keys.c`): not yet checked against curtana.
- Backlight/display: ABL framebuffer only, no panel driver; untested.
