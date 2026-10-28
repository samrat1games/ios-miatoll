/*
 * iMiatoll UEFI: the boot menu. Pick iOS, iOS Settings (GPU on/off, GPU drawing on/off, Metal on/off), Android or power
 * off with the volume keys (up/down) and the power key (select). The keys are read the way the kernel does it
 * (bsd/dev/sweet_keys.c, patch 0060): the PMIC SPMI arbiter's observer channel, read only. Touch is not in the shim yet.
 * If no key arrives for MENU_TIMEOUT_S seconds, or the keys cannot be read, iOS boots with the saved defaults
 * (so a broken menu can never leave the phone stuck).
 */
#include "board.h"
#include "console.h"
#include "menu.h"

#define MENU_TIMEOUT_S	20

#define KEYS_CORE_PA	0x0c440000UL
#define KEYS_OBS_PA	0x0e600000UL
#define KEYS_CNFG_PA	0x0c40a000UL
#define KEYS_PON_RT	0x0810u
#define KEYS_GPIO_RT	0xc110u
#define NO_APID		0xffffu

#define BG	0xff101018U
#define SEL	0xff2a5bd7U
#define DIM	0xff9090a0U

static inline uint32_t rd(uintptr_t a) { uint32_t v = *(volatile uint32_t *)a; __asm__ volatile("dmb oshld" ::: "memory"); return v; }
static inline void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static uint64_t cnt(void) { uint64_t v; __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v)); return v; }
static uint64_t frq(void) { uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return v; }
static void wait_us(unsigned us) { uint64_t e = cnt() + frq() / 1000000 * us; while (cnt() < e) ; }

static uint16_t pon_apid = NO_APID, gpio_apid = NO_APID;

static uint16_t find_apid(uint16_t ppid, uint32_t count)
{
	uint16_t found = NO_APID;
	for (uint32_t i = 0; i < count; ++i) {
		uint32_t map = rd(KEYS_CORE_PA + 0x900u + 4u * i);
		if (map == 0 || ((map >> 8) & 0xfffu) != ppid)
			continue;
		uint32_t owner = rd(KEYS_CNFG_PA + 0x700u + 4u * i) & 7u;
		if (found == NO_APID || owner == 0)
			found = (uint16_t)i;
	}
	return found;
}

static int key_read(uint16_t apid, uint16_t addr, uint8_t *out)
{
	if (apid == NO_APID)
		return 0;
	uint32_t off = 0x80u * apid;
	wr(KEYS_OBS_PA + off, (1u << 27) | ((uint32_t)(addr & 0xffu) << 4));
	__asm__ volatile("dsb osh" ::: "memory");
	for (unsigned i = 0; i < 1000; ++i) {
		uint32_t st = rd(KEYS_OBS_PA + off + 0x08u);
		if (st & 1u) {
			if (st & 0x0eu)
				return 0;
			*out = (uint8_t)rd(KEYS_OBS_PA + off + 0x18u);
			return 1;
		}
		wait_us(1);
	}
	return 0;
}

/* 1 power, 2 volume up, 3 volume down edge; 0 none; -1 keys unreadable */
static int key_event(void)
{
	static int last[3];
	uint8_t pon = 0, gpio = 0;
	int pok = key_read(pon_apid, KEYS_PON_RT, &pon), gok = key_read(gpio_apid, KEYS_GPIO_RT, &gpio);
	if (!pok && !gok)
		return -1;
	int now[3] = { pok && (pon & 1), gok && !(gpio & 1), pok && (pon & 2) };
	int ev = 0;
	for (int k = 0; k < 3; ++k) {
		if (now[k] && !last[k] && !ev)
			ev = k == 0 ? 1 : k == 1 ? 2 : 3;
		last[k] = now[k];
	}
	return ev;
}

static int keys_init(void)
{
	uint32_t ver = rd(KEYS_CORE_PA), count = rd(KEYS_CORE_PA + 4) & 0x7ffu;
	if (ver < 0x50000000u || ver >= 0x70000000u || count == 0 || count > 512)
		return 0;
	pon_apid = find_apid(0x008, count);
	gpio_apid = find_apid(0x4c1, count);
	return pon_apid != NO_APID;
}

extern void menu_android(void);
extern void menu_poweroff(void);

static int gpu_on = 1, draw_on = 1, metal_on = 0;
static char args[96];

static void build_args(void)
{
	char *p = args;
	const char *parts[3] = { gpu_on ? "" : " gpu=0", gpu_on && !draw_on ? " gl=0" : "", metal_on && gpu_on ? " metal=1" : "" };
	*p = 0;
	for (int i = 0; i < 3; ++i)
		for (const char *s = parts[i]; *s && p < args + sizeof(args) - 1; )
			*p++ = *s++;
	*p = 0;
}

static void draw(const char *title, const char *const *items, const char *const *state, int n, int sel)
{
	con_at(1, "iMiatoll UEFI", 0xffffffffU, BG);
	con_at(2, title, DIM, BG);
	for (int i = 0; i < n; ++i) {
		char line[64];
		int k = 0;
		for (const char *s = items[i]; *s && k < 40; ) line[k++] = *s++;
		if (state && state[i])
			for (const char *s = state[i]; *s && k < 62; ) line[k++] = *s++;
		line[k] = 0;
		con_at(4 + i * 2, line, 0xffffffffU, i == sel ? SEL : BG);
	}
	con_at(4 + n * 2 + 1, "Vol +/-  move    Power  select", DIM, BG);
}

/* one list; returns the chosen index, or -1 on timeout / unreadable keys */
static int pick(const char *title, const char *const *items, const char *const *(*states)(void), int n)
{
	int sel = 0;
	uint64_t deadline = cnt() + frq() * MENU_TIMEOUT_S;
	draw(title, items, states ? states() : 0, n, sel);
	for (;;) {
		int e = key_event();
		if (e < 0)
			return -1;
		if (e) {
			deadline = cnt() + frq() * MENU_TIMEOUT_S;
			if (e == 1)
				return sel;
			sel = (sel + (e == 3 ? 1 : n - 1)) % n;
			draw(title, items, states ? states() : 0, n, sel);
		}
		if (cnt() > deadline)
			return -1;
		wait_us(15000);
	}
}

static const char *onoff(int v) { return v ? "  : ON" : "  : OFF"; }
static const char *st_buf[4];
static const char *const *settings_states(void)
{
	st_buf[0] = onoff(gpu_on); st_buf[1] = onoff(draw_on); st_buf[2] = onoff(metal_on); st_buf[3] = 0;
	return st_buf;
}

const char *menu_run(void)
{
	static const char *const main_items[] = { "Boot iOS", "iOS Settings", "Boot Android", "Power off" };
	static const char *const set_items[] = { "GPU", "GPU drawing", "Metal", "Back" };
	con_clear(BG);
	if (!keys_init())
		return "";
	for (;;) {
		int c = pick("Redmi Note 9S", main_items, 0, 4);
		if (c <= 0) {
			build_args();
			return args;
		}
		if (c == 2)
			menu_android();
		if (c == 3)
			menu_poweroff();
		for (;;) {
			int s = pick("iOS Settings", set_items, settings_states, 4);
			if (s < 0 || s == 3)
				break;
			if (s == 0) gpu_on = !gpu_on;
			if (s == 1) draw_on = !draw_on;
			if (s == 2) metal_on = !metal_on;
		}
		con_clear(BG);
	}
}
