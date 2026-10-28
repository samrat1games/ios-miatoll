# Taken from oosexgang/hackios-xnu-kernel (main, 2026-10-28) - read-only copy, nothing was pushed there

Licence of the originals: APSL 2.0 for the XNU-side files (hakios.*), see their LICENSE/NOTICE; wifi/lx is theirs too.
Keep their headers and attribution when editing.

- `../lx/`            the Linux-kernel-API layer (locks, work, memory, firmware, cfg80211/mac80211, wpa, bus) under FreeBSD's LinuxKPI
- `mk-ath11k.sh`      how they build Linux's ath11k (WCN6750) on it: Linux 7.0.14 + FreeBSD 15.1 LinuxKPI headers -> one object
- `hakios.lx.h/.c`    the ~20 `lx_os_*` calls the object needs from the XNU side (memory, threads, sleep, firmware, rproc, options)
- `hakios.wifi.c`     their WLAN bring-up driver (step 15: firmware/QMI/mission mode, then ath11k itself with wifi_lx=1)

## Port plan for WCN3990 (our chip, SM6250, ath10k "snoc")
1. Implement `lx_os_*` on our kernel (bsd/dev/miatoll_lx.c from hakios.lx.c): memory, threads, sleep, `lx_os_firmware` (read from /private/var/miatoll/), `lx_os_radio_memory` (must sit behind the SID 0xc0 SMMU rule, patch 0165), registers at WL_MEM_PA 0x18800000-ish (see sweet_modem.inc).
2. `tools/mk-ath10k.sh`: same as mk-ath11k.sh but Linux's `drivers/net/wireless/ath/ath10k` (core hw htc htt* wmi mac ce bmi txrx snoc qmi qmi_wlfw_v01 ...), licence check ISC/BSD.
3. Replace ath10k_qmi's QMI transport (soc/qcom/qmi.h in lx) by our QRTR/QMI code that already reaches FW_READY + mission mode (sweet_modem.inc).
4. Bridge ath10k's mac80211 station to `wifi/src/redwood_apple80211.c` (scan results -> iOS Settings), as hakios.sta.h does for them.
