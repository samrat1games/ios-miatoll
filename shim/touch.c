/*
 * FT8756 touch for the iMiatoll UEFI menu: the kernel driver's bit-bang SPI (bsd/dev/sweet_touch.c, GPIO53-56 on the
 * TLMM west tile) and firmware download, ported to the bare-metal shim. If the touch firmware already runs (ABL
 * or an earlier boot left it started) nothing is downloaded. Coordinates are the panel's: 1080x2400.
 * Everything here is polled; with the MMU off MMIO is Device memory, so the code stays aligned (-mstrict-align).
 */
#include <stdint.h>
#include <stdbool.h>
#include "touch.h"

#define EINVAL 22
#define EIO 5
#define ETIMEDOUT 60
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define TOUCH_MAX 10u

static inline uint32_t rd(uintptr_t a) { uint32_t v = *(volatile uint32_t *)a; __asm__ volatile("dmb oshld" ::: "memory"); return v; }
static inline void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; __asm__ volatile("dsb osh" ::: "memory"); }
static uint64_t cnt(void) { uint64_t v; __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v)); return v; }
static uint64_t frq(void) { uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return v; }
void wait_us(unsigned us) { uint64_t e = cnt() + frq() / 1000000 * us; while (cnt() < e) ; }
static void tw(unsigned ms) { wait_us(ms * 1000u); }

void *memset(void *d, int c, unsigned long n)
{
	volatile uint8_t *p = d;
	while (n--)
		*p++ = (uint8_t)c;
	return d;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
	uint8_t *p = d;
	const uint8_t *q = s;
	while (n--) {
		*p++ = *q++;
		__asm__ volatile("" ::: "memory");
	}
	return d;
}

static int tp_down;
static int tp_x, tp_y;
#define TSPI_WEST_PA    0x03500000u     /* TLMM WEST tile: GPIO53..56 */
#define TSPI_PIN_BASE   53u
#define TSPI_CMD_READ   0xa0u           /* READ_CMD: 0x80 | DATA_CRC_EN */
#define TSPI_DUMMY      3u
#define TSPI_HDR        4u
#define TSPI_MAXRD      62u             /* touch frame: 1 + 3 + 6 * 10 - 2 */

static struct {
	unsigned miso, mosi, clk, cs;
	bool in_rom;
} sp;

#define TSPI_CFG(n)     ((uint32_t)(n) * 0x1000u)
#define TSPI_IO(n)      (TSPI_CFG(n) + 4u)

static void
tspi_pin(unsigned pin, bool out, bool high)
{
	uint32_t cfg = rd(TSPI_WEST_PA + TSPI_CFG(pin));
	if (out) {
		wr(TSPI_WEST_PA + TSPI_IO(pin), high ? 2u : 0u);
	}
	cfg &= ~0x3ffu;                  /* mux 0 = GPIO, no pull */
	cfg |= (2u << 6) | (out ? 0x200u : 0u); /* 6 mA */
	wr(TSPI_WEST_PA + TSPI_CFG(pin), cfg);
}

static inline void
tspi_out(unsigned pin, bool high)
{
	wr(TSPI_WEST_PA + TSPI_IO(pin), high ? 2u : 0u);
}

static inline unsigned
tspi_in(unsigned pin)
{
	return rd(TSPI_WEST_PA + TSPI_IO(pin)) & 1u;
}

static void
tspi_xfer(const uint8_t *tx, uint8_t *rx, unsigned len)
{
	tspi_out(sp.cs, false);
	wait_us(2);
	for (unsigned i = 0; i < len; ++i) {
		uint8_t o = tx[i], v = 0;
		for (int b = 7; b >= 0; --b) {
			tspi_out(sp.mosi, (o >> b) & 1u);
			wait_us(1);
			tspi_out(sp.clk, true);
			wait_us(1);
			v = (uint8_t)((v << 1) | tspi_in(sp.miso));
			tspi_out(sp.clk, false);
		}
		rx[i] = v;
	}
	wait_us(2);
	tspi_out(sp.cs, true);
}

/* CRC-16/KERMIT as in the vendor driver (init 0xffff, poly 0x8408 reflected). */
static uint16_t
tspi_crc(const uint8_t *d, unsigned len)
{
	uint16_t crc = 0xffffu;
	for (unsigned i = 0; i < len; ++i) {
		crc ^= d[i];
		for (unsigned j = 0; j < 8; ++j) {
			crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0x8408u) : (uint16_t)(crc >> 1);
		}
	}
	return crc;
}

/* fts_read(): header cmd,ctrl,len_hi,len_lo, 3 dummy, data, crc16 (LE). */
static int
tspi_read(uint8_t cmd, uint8_t *data, unsigned len)
{
	uint8_t tx[TSPI_HDR + TSPI_DUMMY + TSPI_MAXRD + 2];
	uint8_t rx[sizeof(tx)];
	unsigned total = TSPI_HDR + TSPI_DUMMY + len + 2u;
	if (!len || len > TSPI_MAXRD) {
		return EINVAL;
	}
	for (unsigned t = 0; t < 3; ++t) {
		memset(tx, 0, sizeof(tx));
		tx[0] = cmd;
		tx[1] = TSPI_CMD_READ;
		tx[2] = (uint8_t)(len >> 8);
		tx[3] = (uint8_t)len;
		tspi_xfer(tx, rx, total);
		wait_us(150);
		if (rx[3] & 0xa0u) {
			continue;       /* status not ready */
		}
		const uint8_t *d = rx + TSPI_HDR + TSPI_DUMMY;
		uint16_t want = (uint16_t)(d[len] | ((unsigned)d[len + 1] << 8));
		if (tspi_crc(d, len) != want) {
			continue;
		}
		memcpy(data, d, len);
		return 0;
	}
	return EIO;
}

static int
tspi_reg(uint8_t reg, uint8_t *v)
{
	return tspi_read(reg, v, 1);
}

static int
tspi_touch(void)
{
	uint8_t d[TSPI_MAXRD];
	int x[TOUCH_MAX] = {0}, y[TOUCH_MAX] = {0};
	uint32_t mask = 0;
	/* Vendor buffer: buf[0]=cmd 0x01, buf[1..] = data; points at buf[3 + 6i]. */
	int err = tspi_read(0x01, d, sizeof(d));
	if (err) {
		return err;
	}
	if ((d[0] & 0xf0u) != 0x90u) {
		return EIO;
	}
	unsigned n = d[1] & 15u;
	if (n > TOUCH_MAX) {
		return EIO;
	}
	unsigned active = 0;
	for (unsigned i = 0; i < TOUCH_MAX; ++i) {
		const uint8_t *p = d + 2u + 6u * i;
		unsigned id = p[2] >> 4, event = p[0] >> 6;
		if (id >= 0x0au) {
			break;          /* unused record */
		}
		if (id >= TOUCH_MAX || (mask & (1u << id))) {
			return EIO;
		}
		if (event == 1u) {
			continue;       /* UP */
		}
		if (event == 3u) {
			return EIO;
		}
		unsigned px = ((unsigned)(p[0] & 15u) << 8) | p[1];
		unsigned py = ((unsigned)(p[2] & 15u) << 8) | p[3];
		if (px > 1079 || py > 2399) {
			return EIO;
		}
		mask |= 1u << id;
		x[id] = (int)px;
		y[id] = (int)py;
		++active;
	}
	if (n == 0 && active) {
		return EIO;
	}
	tp_down = mask != 0;
	for (unsigned k = 0; k < TOUCH_MAX; ++k) {
		if (mask & (1u << k)) {
			tp_x = x[k];
			tp_y = y[k];
			break;
		}
	}
	return 0;
}

/*
 * FT8756 has no firmware of its own: after power-up it sits in its boot ROM (normal-mode registers read
 * 0xef, boot id 0x8756, reg 0xd0 == 1) and the host downloads the touch firmware into PRAM/DRAM, checks it
 * with the ROM's ECC command and starts it (vendor focaltech_flash.c: fts_fw_recovery -> fts_fw_resume ->
 * fts_fw_write_start with need_reset=false, i.e. NO reset pulse). Firmware: vendor fw_huaxing_v0e
 * (panel "huaxing", what Android reports as fw 0e), 176 KiB, in sweet_touch_fw.inc.
 */
#include "touch_fw.inc"

#define TFW_PKT         32752u          /* FTS_FLASH_PACKET_LENGTH_SPI = 32 KiB - 16 */
#define TFW_ECC_PKT     32766u          /* upgrade_setting_nf.ecclen_max */
#define TFW_APP_INFO    0x100u
#define TFW_DRAM_BASE   0xd00000u

static uint8_t tfw_tx[TSPI_HDR + TSPI_DUMMY + TFW_PKT + 16];
static uint8_t tfw_rx[sizeof(tfw_tx)];

/* fts_write(): header cmd,0x00,len_hi,len_lo (len = bytes after cmd), 3 dummy, data. */
static int
tspi_write(const uint8_t *buf, unsigned len)
{
	unsigned datalen = len - 1, total;
	if (!len || datalen > TFW_PKT + 8) {
		return EINVAL;
	}
	for (unsigned t = 0; t < 3; ++t) {
		memset(tfw_tx, 0, TSPI_HDR + TSPI_DUMMY + datalen);
		tfw_tx[0] = buf[0];
		tfw_tx[2] = (uint8_t)(datalen >> 8);
		tfw_tx[3] = (uint8_t)datalen;
		total = TSPI_HDR;
		if (datalen) {
			total += TSPI_DUMMY;
			memcpy(tfw_tx + total, buf + 1, datalen);
			total += datalen;
		}
		tspi_xfer(tfw_tx, tfw_rx, total);
		wait_us(150);
		if (!(tfw_rx[3] & 0xa0u)) {
			return 0;
		}
	}
	return EIO;
}

static uint16_t
tfw_ecc_host(const uint8_t *d, unsigned len)
{
	uint16_t ecc = 0;
	for (unsigned i = 0; i < len; i += 2) {
		ecc ^= (uint16_t)((d[i] << 8) | d[i + 1]);
		for (unsigned j = 0; j < 16; ++j) {
			ecc = (ecc & 1u) ? (uint16_t)((ecc >> 1) ^ 0x8408u) : (uint16_t)(ecc >> 1);
		}
	}
	return ecc;
}

static int
tfw_ecc_tp(uint32_t saddr, uint32_t len, uint16_t *out)
{
	uint8_t cmd[7] = {0xcc, (uint8_t)(saddr >> 16), (uint8_t)(saddr >> 8), (uint8_t)saddr,
	    (uint8_t)(len >> 16), (uint8_t)(len >> 8), (uint8_t)len};
	uint8_t v[2] = {0, 0};
	int err = tspi_write(cmd, sizeof(cmd));
	if (err) {
		return err;
	}
	tw(2);
	unsigned i;
	for (i = 0; i < 100; ++i) {
		err = tspi_read(0xce, v, 1);
		if (err) {
			return err;
		}
		if (v[0] == 0xa5u) {            /* eccok_val */
			break;
		}
		tw(1);
	}
	if (i >= 100) {
		return ETIMEDOUT;
	}
	err = tspi_read(0xcd, v, 2);
	*out = (uint16_t)((v[0] << 8) | v[1]);
	return err;
}

static int
tfw_ecc_check(const uint8_t *buf, uint32_t len, uint32_t saddr)
{
	for (uint32_t off = 0; off < len; off += TFW_ECC_PKT) {
		uint32_t n = MIN(len - off, TFW_ECC_PKT);
		uint16_t tp = 0, host = tfw_ecc_host(buf + off, n);
		int err = tfw_ecc_tp(saddr + off, n, &tp);
		if (err) {
			return err;
		}
		if (tp != host) {
			return EIO;
		}
	}
	return 0;
}

/* fts_dpram_write(): per packet set the RAM address (0xad), then write the data (0xae). */
static int
tfw_ram_write(uint32_t saddr, uint32_t base, const uint8_t *buf, uint32_t len)
{
	static uint8_t cmd[1 + TFW_PKT];
	for (uint32_t off = 0; off < len; off += TFW_PKT) {
		uint32_t n = MIN(len - off, TFW_PKT), a = saddr + off + base;
		uint8_t acmd[4] = {0xad, (uint8_t)(a >> 16), (uint8_t)(a >> 8), (uint8_t)a};
		int err = tspi_write(acmd, sizeof(acmd));
		if (err) {
			return err;
		}
		cmd[0] = 0xae;
		memcpy(cmd + 1, buf + off, n);
		err = tspi_write(cmd, 1 + n);
		if (err) {
			return err;
		}
	}
	return 0;
}

static int
tfw_download(void)
{
	const uint8_t *fw = sweet_touch_fw;
	const unsigned len = sizeof(sweet_touch_fw);
	uint16_t code = (uint16_t)((fw[TFW_APP_INFO] << 8) | fw[TFW_APP_INFO + 1]);
	uint16_t coden = (uint16_t)((fw[TFW_APP_INFO + 2] << 8) | fw[TFW_APP_INFO + 3]);
	if (len < 0x120 || (uint16_t)(code + coden) != 0xffffu) {
		return EINVAL;
	}
	uint32_t pram = (uint32_t)code * 2u;            /* half_length */
	int err = tfw_ram_write(0, 0, fw, pram);
	if (!err) {
		err = tfw_ecc_check(fw, pram, 0);
	}
	if (err) {
		return err;
	}
	uint16_t cl = (uint16_t)((fw[TFW_APP_INFO + 8] << 8) | fw[TFW_APP_INFO + 9]);
	uint16_t cln = (uint16_t)((fw[TFW_APP_INFO + 10] << 8) | fw[TFW_APP_INFO + 11]);
	if ((uint16_t)(cl + cln) == 0xffffu && cl != 0) {   /* drwr_support */
		uint32_t dram = (uint32_t)cl * 2u;
		err = tfw_ram_write(0, TFW_DRAM_BASE, fw + pram, dram);
		if (!err) {
			err = tfw_ecc_check(fw + pram, dram, 0);   /* vendor passes saddr 0 here too */
		}
		if (err) {
			return err;
		}
	}
	uint8_t start = 0x08;                           /* FTS_ROMBOOT_CMD_START_APP */
	err = tspi_write(&start, 1);
	return err;
}

/* Boot ROM answers cmd 0x90 with the chip id and reg 0xd0 reads 1 (upgsts_boot). */
static bool
tfw_in_boot(void)
{
	uint8_t id[2] = {0, 0}, state = 0;
	int e1 = tspi_read(0x90, id, 2);
	int e2 = tspi_reg(0xd0, &state);
	return !e1 && !e2 && id[0] == 0x87u && id[1] == 0x56u && state == 0x01u;
}

static bool
tspi_probe(unsigned miso, unsigned mosi, unsigned clk, unsigned cs)
{
	uint8_t h = 0, l = 0;
	sp.miso = miso;
	sp.mosi = mosi;
	sp.clk = clk;
	sp.cs = cs;
	tspi_pin(cs, true, true);
	tspi_pin(clk, true, false);
	tspi_pin(mosi, true, false);
	tspi_pin(miso, false, false);
	tw(5);
	int e1 = tspi_reg(0xa3, &h);
	int e2 = tspi_reg(0x9f, &l);
	sp.in_rom = !e1 && !e2 && h == 0xefu && l == 0xefu;
	return !e1 && !e2 && h == 0x87u && l == 0x56u;
}
/* 1 when the controller answers (after loading its firmware if it was in the boot ROM), 0 otherwise */
int touch_init(void)
{
	const unsigned b = TSPI_PIN_BASE;
	tw(20);
	bool ok = tspi_probe(b, b + 1, b + 2, b + 3);
	if (!ok && sp.in_rom && tfw_in_boot() && tfw_download() == 0) {
		for (unsigned i = 0; i < 40 && !ok; ++i) {
			tw(50);
			ok = tspi_probe(b, b + 1, b + 2, b + 3);
		}
	}
	return ok;
}

/* 1 finger down (x, y set), 0 none, -1 read error */
int touch_poll(int *x, int *y)
{
	if (tspi_touch())
		return -1;
	*x = tp_x;
	*y = tp_y;
	return tp_down;
}
