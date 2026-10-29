/*
 * iMiatoll UEFI: the boot menu, drawn like the iOS recovery screen (title, grey subtitle, one rounded card of rows with an
 * icon, a title, a line of grey text and a chevron or a switch, a language button top left, a power button top right).
 * Pages: main (Boot iOS, iOS Settings, Boot Android, Power off), iOS Settings (GPU, GPU drawing, Metal, Wi-Fi, Bluetooth,
 * Tests), Tests (touch, buttons, display; the rest are listed and say they run inside iOS).
 *
 * Input: touch (FT8756, touch.c) and the keys, read the way the kernel does it (bsd/dev/sweet_keys.c, patch 0060): the PMIC
 * SPMI arbiter's observer channel, read only. Volume up/down move, power selects. If nothing is touched or pressed for
 * MENU_TIMEOUT_S seconds, or neither input works, iOS boots with the saved defaults (so a broken menu never leaves the
 * phone stuck). Languages: English, Russian, Ukrainian.
 * No pointer tables: the image has no relocation pass, strings come from switches.
 */
#include "board.h"
#include "console.h"
#include "menu.h"
#include "touch.h"
#include "theme_gen.h"

#define MENU_TIMEOUT_S	20

#define KEYS_CORE_PA	0x0c440000UL
#define KEYS_OBS_PA	0x0e600000UL
#define KEYS_CNFG_PA	0x0c40a000UL
#define KEYS_PON_RT	0x0810u
#define KEYS_GPIO_RT	0xc110u
#define NO_APID		0xffffu

#define BG	0xfff4f5faU
#define CARD	0xffe8eaf2U
#define SELC	0xffc9d6f5U
#define INK	0xff111116U
#define GREY	0xff6b6b76U
#define LINE	0xffcfd2dcU
#define BLUEC	0xff3b7be0U
#define GREENC	0xff34c759U
#define REDC	0xffe5484dU
#define SLATE	0xff7d8497U
#define OFFC	0xffc3c6d0U
#define WHITEC	0xffffffffU

#define CARD_X	107
#define CARD_W	866
#define CARD_Y	640
#define ROW_H	180

#ifndef HOST_TEST
static inline uint32_t rd(uintptr_t a) { uint32_t v = *(volatile uint32_t *)a; __asm__ volatile("dmb oshld" ::: "memory"); return v; }
static inline void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static uint64_t cnt(void) { uint64_t v; __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v)); return v; }
static uint64_t frq(void) { uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return v; }
#endif

/* ------------------------------------------------------------------ keys */

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

/* levels: [0] power, [1] volume up, [2] volume down; 0 when the keys cannot be read */
static int key_levels(int *lv)
{
	uint8_t pon = 0, gpio = 0;
	int pok = key_read(pon_apid, KEYS_PON_RT, &pon), gok = key_read(gpio_apid, KEYS_GPIO_RT, &gpio);
	if (!pok && !gok)
		return 0;
	lv[0] = pok && (pon & 1);
	lv[1] = gok && !(gpio & 1);
	lv[2] = pok && (pon & 2);
	return 1;
}

/* 1 power, 2 volume up, 3 volume down edge; 0 none; -1 keys unreadable */
static int key_event(void)
{
	static int last[3];
	int now[3];
	if (!key_levels(now))
		return -1;
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

/* ------------------------------------------------------------------ drawing */

#define FBW	(FB_STRIDE / 4)

static void fill_rect(int x, int y, int w, int h, uint32_t c)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > FB_W) w = FB_W - x;
	if (y + h > FB_H) h = FB_H - y;
	for (int j = 0; j < h; ++j) {
		volatile uint32_t *p = con_fb + (y + j) * FBW + x;
		for (int i = 0; i < w; ++i)
			p[i] = c;
	}
}

static void fill_round(int x, int y, int w, int h, int r, uint32_t c)
{
	for (int j = 0; j < h; ++j) {
		int dy = j < r ? r - j : j >= h - r ? j - (h - r - 1) : 0, inset = 0;
		if (dy > 0)
			while ((r - inset) * (r - inset) + dy * dy > r * r)
				++inset;
		fill_rect(x + inset, y + j, w - 2 * inset, 1, c);
	}
}

static void fill_circle(int cx, int cy, int r, uint32_t c)
{
	for (int j = -r; j <= r; ++j) {
		int d = 0;
		while ((d + 1) * (d + 1) + j * j <= r * r)
			++d;
		fill_rect(cx - d, cy + j, 2 * d + 1, 1, c);
	}
}

/* ring: a pixel is on when its squared distance is between (r-t)^2 and r^2; gap_top cuts the top (power symbol) */
static void ring(int cx, int cy, int r, int t, uint32_t c, int gap_top)
{
	for (int j = -r; j <= r; ++j)
		for (int i = -r; i <= r; ++i) {
			int d = i * i + j * j;
			if (d > r * r || d < (r - t) * (r - t))
				continue;
			if (gap_top && j < 0 && i > -14 && i < 14)
				continue;
			fill_rect(cx + i, cy + j, 1, 1, c);
		}
}

/* UTF-8 decode of one code point */
static uint32_t next_cp(const char **ps)
{
	const unsigned char *s = (const unsigned char *)*ps;
	uint32_t cp = s[0];
	int n = 1;
	if (cp >= 0xe0) { cp &= 0x0f; n = 3; }
	else if (cp >= 0xc0) { cp &= 0x1f; n = 2; }
	for (int i = 1; i < n && s[i]; ++i)
		cp = (cp << 6) | (s[i] & 0x3f);
	*ps += n;
	return cp;
}

/* scale/bold of the old bitmap font -> an SF size id (see tools/gen-sf.py: 96 bold, 56, 58 semibold, 42, 36) */
static int size_id(int scale, int bold)
{
	return scale >= 6 ? 0 : scale == 5 ? 2 : scale == 4 ? (bold ? 2 : 1) : scale == 3 ? (bold ? 3 : 4) : 4;
}

static const struct sf_glyph *sf_find(int sid, uint32_t cp)
{
	const struct sf_size *z = &sf_sizes[sid];
	for (unsigned k = 0; k < z->count; ++k)
		if (sf_glyphs[z->first + k].cp == cp)
			return &sf_glyphs[z->first + k];
	return cp == '?' ? 0 : sf_find(sid, '?');
}

static int text_w(const char *s, int scale)
{
	int sid = size_id(scale, scale == 3), w = 0;
	while (*s) {
		const struct sf_glyph *g = sf_find(sid, next_cp(&s));
		if (g)
			w += g->adv;
	}
	return w;
}

static int text_w_n(const char *s, const char *e)
{
	int sid = size_id(4, 0), w = 0;
	while (s < e) {
		const struct sf_glyph *g = sf_find(sid, next_cp(&s));
		if (g)
			w += g->adv;
	}
	return w;
}

static uint32_t mix(uint32_t bg, uint32_t fg, unsigned a)
{
	uint32_t r = 0xff000000U;
	for (int sh = 0; sh <= 16; sh += 8) {
		unsigned b = (bg >> sh) & 0xff, f = (fg >> sh) & 0xff;
		r |= (uint32_t)((b * (255 - a) + f * a + 127) / 255) << sh;
	}
	return r;
}

/* blend an 8-bit mask over what is on screen now */
static void blit_mask(int x, int y, int w, int h, const uint8_t *m, uint32_t fg)
{
	for (int j = 0; j < h; ++j) {
		if (y + j < 0 || y + j >= FB_H)
			continue;
		volatile uint32_t *p = con_fb + (y + j) * FBW;
		for (int i = 0; i < w; ++i) {
			unsigned a = m[j * w + i];
			if (a && x + i >= 0 && x + i < FB_W)
				p[x + i] = a == 255 ? fg : mix(p[x + i], fg, a);
		}
	}
}

/* y is the top of a text line of the old bitmap size; the baseline follows from the SF size */
static void text(int x, int y, int scale, const char *s, uint32_t c, int bold)
{
	int sid = size_id(scale, scale == 3 ? 1 : bold);
	int base = y + sf_sizes[sid].asc;
	while (*s) {
		const struct sf_glyph *g = sf_find(sid, next_cp(&s));
		if (!g)
			continue;
		if (g->w && g->h)
			blit_mask(x + g->l, base + g->t, g->w, g->h, sf_blob + g->off, c);
		x += g->adv;
	}
}

static void icon(int id, int cx, int cy, uint32_t c)
{
	const struct sf_icon *ic = &sf_icons[id];
	blit_mask(cx - ic->w / 2, cy - ic->h / 2, ic->w, ic->h, sf_blob + ic->off, c);
}

/* ------------------------------------------------------------------ strings (switches, no pointer tables) */

static int lang;	/* 0 English, 1 Russian, 2 Ukrainian, 3 German, 4 Spanish, 5 French */
#define NLANG 6

#define L6(en, ru, ua, de, es, fr) (lang == 1 ? ru : lang == 2 ? ua : lang == 3 ? de : lang == 4 ? es : lang == 5 ? fr : en)

enum { P_MAIN, P_SET, P_TEST, P_LANG, P_ABOUT };

static int page_rows(int p) { return p == P_MAIN ? 5 : p == P_SET ? 7 : p == P_TEST ? 8 : p == P_LANG ? NLANG + 1 : 5; }
static int card_y(int p) { return p == P_ABOUT ? 1290 : CARD_Y; }

static const char *page_title(int p)
{
	return p == P_MAIN ? "iMiatoll UEFI" :
	    p == P_SET ? L6("iOS Settings", "Настройки iOS", "Налаштування iOS", "iOS-Einstellungen", "Ajustes de iOS", "Réglages iOS") :
	    p == P_TEST ? L6("Tests", "Тесты", "Тести", "Tests", "Pruebas", "Tests") :
	    p == P_LANG ? L6("Language", "Язык", "Мова", "Sprache", "Idioma", "Langue") : "About iMiatoll";
}

static const char *page_sub(int p)
{
	if (p == P_MAIN)
		return L6("Choose how to start your Redmi Note 9S.", "Выберите, как запустить Redmi Note 9S.",
		    "Оберіть, як запустити Redmi Note 9S.", "Wähle, wie dein Redmi Note 9S startet.",
		    "Elige cómo iniciar tu Redmi Note 9S.", "Choisis comment démarrer ton Redmi Note 9S.");
	if (p == P_SET)
		return L6("Applied at the next iOS boot.", "Применяется при следующей загрузке iOS.",
		    "Застосовується при наступному запуску iOS.", "Gilt beim nächsten iOS-Start.",
		    "Se aplica en el próximo inicio de iOS.", "Appliqué au prochain démarrage d'iOS.");
	if (p == P_TEST)
		return L6("Checks that run here, before iOS.", "Проверки, которые идут здесь, до iOS.",
		    "Перевірки, що йдуть тут, до iOS.", "Prüfungen hier, vor iOS.", "Pruebas aquí, antes de iOS.",
		    "Vérifications ici, avant iOS.");
	if (p == P_LANG)
		return L6("The menu language.", "Язык меню.", "Мова меню.", "Die Sprache des Menüs.", "El idioma del menú.",
		    "La langue du menu.");
	return L6("Meet Mitlk, the mascot.", "Знакомьтесь: Mitlk, наш маскот.", "Знайомтесь: Mitlk, наш маскот.",
	    "Das ist Mitlk, das Maskottchen.", "Te presentamos a Mitlk, la mascota.", "Voici Mitlk, la mascotte.");
}

static const char *lang_name(int i)
{
	return i == 0 ? "English" : i == 1 ? "Русский" : i == 2 ? "Українська" : i == 3 ? "Deutsch" : i == 4 ? "Español" : "Français";
}

static const char *lang_en(int i)
{
	return i == 0 ? "English" : i == 1 ? "Russian" : i == 2 ? "Ukrainian" : i == 3 ? "German" : i == 4 ? "Spanish" : "French";
}

static const char *back_text(void) { return L6("Back", "Назад", "Назад", "Zurück", "Atrás", "Retour"); }

static const char *row_title(int p, int i)
{
	if (p == P_MAIN)
		return i == 0 ? L6("Boot iOS", "Запустить iOS", "Запустити iOS", "iOS starten", "Iniciar iOS", "Démarrer iOS") :
		    i == 1 ? L6("iOS Settings", "Настройки iOS", "Налаштування iOS", "iOS-Einstellungen", "Ajustes de iOS", "Réglages iOS") :
		    i == 2 ? L6("Boot Android", "Запустить Android", "Запустити Android", "Android starten", "Iniciar Android", "Démarrer Android") :
		    i == 3 ? "About iMiatoll" : L6("Power off", "Выключить", "Вимкнути", "Ausschalten", "Apagar", "Éteindre");
	if (p == P_LANG)
		return i < NLANG ? lang_name(i) : back_text();
	if (p == P_ABOUT)
		return i == 0 ? L6("Version", "Версия", "Версія", "Version", "Versión", "Version") :
		    i == 1 ? L6("Kernel", "Ядро", "Ядро", "Kernel", "Núcleo", "Noyau") :
		    i == 2 ? L6("Device", "Устройство", "Пристрій", "Gerät", "Dispositivo", "Appareil") :
		    i == 3 ? L6("Credits", "Авторы", "Автори", "Mitwirkende", "Créditos", "Crédits") : back_text();
	if (p == P_SET)
		return i == 0 ? "GPU" : i == 1 ? L6("GPU drawing", "Рисование GPU", "Малювання GPU", "GPU-Zeichnen", "Dibujo por GPU", "Dessin GPU") :
		    i == 2 ? "Metal" : i == 3 ? L6("Wi-Fi (modem)", "Wi-Fi (модем)", "Wi-Fi (модем)", "WLAN (Modem)", "Wi-Fi (módem)", "Wi-Fi (modem)") :
		    i == 4 ? "Bluetooth" : i == 5 ? L6("Tests", "Тесты", "Тести", "Tests", "Pruebas", "Tests") : back_text();
	return i == 0 ? L6("Touch test", "Тест тача", "Тест тачу", "Touch-Test", "Prueba táctil", "Test tactile") :
	    i == 1 ? L6("Buttons test", "Тест кнопок", "Тест кнопок", "Tastentest", "Prueba de botones", "Test des boutons") :
	    i == 2 ? L6("Display test", "Тест дисплея", "Тест дисплея", "Displaytest", "Prueba de pantalla", "Test de l'écran") :
	    i == 3 ? L6("Flashlight", "Фонарик", "Ліхтарик", "Taschenlampe", "Linterna", "Lampe torche") :
	    i == 4 ? "Wi-Fi" : i == 5 ? "Bluetooth" : i == 6 ? "Metal" : back_text();
}

static const char *row_sub(int p, int i)
{
	if (p == P_MAIN)
		return i == 0 ? L6("Start the system", "Запуск системы", "Запуск системи", "System starten", "Arrancar el sistema", "Lancer le système") :
		    i == 1 ? L6("GPU, Metal, Wi-Fi, tests", "GPU, Metal, Wi-Fi, тесты", "GPU, Metal, Wi-Fi, тести", "GPU, Metal, WLAN, Tests",
		    "GPU, Metal, Wi-Fi, pruebas", "GPU, Metal, Wi-Fi, tests") :
		    i == 2 ? L6("Restart into Android", "Перезапуск в Android", "Перезапуск в Android", "Neustart in Android",
		    "Reiniciar en Android", "Redémarrer sous Android") :
		    i == 3 ? L6("Version 1.0.0, Mitlk", "Версия 1.0.0, Mitlk", "Версія 1.0.0, Mitlk", "Version 1.0.0, Mitlk",
		    "Versión 1.0.0, Mitlk", "Version 1.0.0, Mitlk") :
		    L6("Turn the phone off", "Выключить телефон", "Вимкнути телефон", "Telefon ausschalten", "Apagar el teléfono",
		    "Éteindre le téléphone");
	if (p == P_LANG)
		return i < NLANG ? lang_en(i) : "";
	if (p == P_ABOUT)
		return i == 0 ? "1.0.0" : i == 1 ? "XNU 8796.141.3" : i == 2 ? "Redmi Note 9S (curtana, SM6250)" :
		    i == 3 ? "Samrat, HakiOS (oosexgang)" : "";
	if (p == P_SET)
		return i == 0 ? "Adreno 618" :
		    i == 1 ? L6("Draw on the GPU", "Рисовать на GPU", "Малювати на GPU", "Auf der GPU zeichnen", "Dibujar en la GPU", "Dessiner sur le GPU") :
		    i == 2 ? L6("hakios-metal layer", "слой hakios-metal", "шар hakios-metal", "hakios-metal-Schicht", "capa hakios-metal",
		    "couche hakios-metal") :
		    i == 3 ? L6("WCN3990 over the modem", "WCN3990 через модем", "WCN3990 через модем", "WCN3990 über das Modem",
		    "WCN3990 por el módem", "WCN3990 via le modem") :
		    i == 4 ? L6("Not ready in the kernel", "Ядро пока не умеет", "Ядро поки не вміє", "Im Kernel noch nicht fertig",
		    "Aún no está en el núcleo", "Pas encore dans le noyau") :
		    i == 5 ? L6("Touch, buttons, display", "Тач, кнопки, дисплей", "Тач, кнопки, дисплей", "Touch, Tasten, Display",
		    "Táctil, botones, pantalla", "Tactile, boutons, écran") : "";
	return i == 0 ? L6("Draw with a finger", "Рисуйте пальцем", "Малюйте пальцем", "Mit dem Finger zeichnen", "Dibuja con el dedo", "Dessine du doigt") :
	    i == 1 ? L6("Volume and power keys", "Клавиши громкости и питания", "Клавіші гучності й живлення", "Lautstärke- und Ein-Taste",
	    "Teclas de volumen y encendido", "Touches volume et marche") :
	    i == 2 ? L6("Colour bars", "Цветные полосы", "Кольорові смуги", "Farbbalken", "Barras de color", "Barres de couleur") :
	    i == 7 ? "" : L6("Runs in the iOS test boot", "Идёт в тестовой загрузке iOS", "Іде в тестовому запуску iOS", "Läuft im iOS-Testlauf",
	    "Se ejecuta en el arranque de prueba", "S'exécute au démarrage de test");
}

/* ------------------------------------------------------------------ state */

extern void menu_android(void);
extern void menu_poweroff(void);

static int opt[5] = { 1, 1, 0, 1, 0 };	/* GPU drawing works since gpu_trace_ms=0 (the driver slept 300 ms after every log line) */	/* GPU, GPU drawing, Metal, Wi-Fi, Bluetooth */
static char args[128];

static void build_args(void)
{
	char *p = args;
	const char *s0 = " gpu_trace_ms=0 ufsroot=ios";	/* dual boot: iOS has its own partition "ios" (the kernel falls back to "userdata" when there is none) */
	while (*s0)
		*p++ = *s0++;
	for (int i = 0; i < 5; ++i) {
		const char *s = i == 0 ? (opt[0] ? "" : " gpu=0") : i == 1 ? (opt[0] && !opt[1] ? " gl=0" : "") :
		    i == 2 ? (opt[0] && opt[2] ? " metal=1" : "") : i == 3 ? (opt[3] ? "" : " sweetmodem=0") :
		    (opt[4] ? " sweetbt=1" : "");
		while (*s && p < args + sizeof(args) - 1)
			*p++ = *s++;
	}
	*p = 0;
}

static int has_touch;

static uint32_t icon_color(int p, int i)
{
	if (p == P_MAIN)
		return i == 0 ? BLUEC : i == 1 ? SLATE : i == 2 ? GREENC : i == 3 ? 0xff8e63d8U : REDC;
	if (p == P_LANG)
		return i == lang ? BLUEC : SLATE;
	return i == page_rows(p) - 1 ? SLATE : BLUEC;
}

static int icon_id(int p, int i)
{
	if (i == page_rows(p) - 1 && p != P_MAIN)
		return ICON_CHEVRON_LEFT_52;
	if (p == P_MAIN)
		return i == 0 ? ICON_APPLE_LOGO_52 : i == 1 ? ICON_GEARSHAPE_FILL_52 : i == 2 ? ICON_ANDROID_52 :
		    i == 3 ? ICON_CHECKMARK_CIRCLE_FILL_52 : ICON_POWER_52;
	if (p == P_LANG)
		return ICON_GLOBE_52;
	if (p == P_ABOUT)
		return i == 0 ? ICON_CHECKMARK_CIRCLE_FILL_52 : i == 1 ? ICON_CPU_52 : i == 2 ? ICON_IPHONE_52 : ICON_GLOBE_52;
	if (p == P_SET)
		return i == 0 ? ICON_CPU_52 : i == 1 ? ICON_PAINTBRUSH_52 : i == 2 ? ICON_CUBE_52 : i == 3 ? ICON_WIFI_52 :
		    i == 4 ? ICON_DOT_RADIOWAVES_LEFT_AND_RIGHT_52 : ICON_CHECKMARK_CIRCLE_FILL_52;
	return i == 0 ? ICON_HAND_POINT_UP_LEFT_52 : i == 1 ? ICON_KEYBOARD_52 : i == 2 ? ICON_DISPLAY_52 :
	    i == 3 ? ICON_FLASHLIGHT_ON_FILL_52 : i == 4 ? ICON_WIFI_52 : i == 5 ? ICON_DOT_RADIOWAVES_LEFT_AND_RIGHT_52 :
	    ICON_CUBE_52;
}

/* 0 chevron, 1 switch, 2 grey, 3 selected mark, 4 nothing (information) */
static int row_kind(int p, int i)
{
	if (p == P_SET && i < 5)
		return 1;
	if (p == P_TEST && i >= 3 && i < 7)
		return 2;
	if (p == P_LANG && i < NLANG)
		return i == lang ? 3 : 4;
	if (p == P_ABOUT && i < 4)
		return 4;
	return 0;
}

static void draw_row(int p, int i, int sel)
{
	int y = card_y(p) + i * ROW_H;
	int cy = y + ROW_H / 2;
	fill_round(CARD_X + 14, y + 8, CARD_W - 28, ROW_H - 16, 34, i == sel ? SELC : CARD);
	fill_circle(CARD_X + 90, cy, 46, icon_color(p, i));
	icon(icon_id(p, i), CARD_X + 90, cy, WHITEC);
	int k = row_kind(p, i);
	text(CARD_X + 180, y + 20, 4, row_title(p, i), k == 2 ? GREY : INK, 1);
	text(CARD_X + 180, y + 96, 3, row_sub(p, i), GREY, 0);
	if (k == 0) {
		icon(ICON_CHEVRON_RIGHT_40, CARD_X + CARD_W - 70, cy, SLATE);
	} else if (k == 1) {
		int on = opt[i], sx = CARD_X + CARD_W - 190;
		fill_round(sx, cy - 36, 130, 72, 36, on ? GREENC : OFFC);
		fill_circle(on ? sx + 130 - 36 : sx + 36, cy, 30, WHITEC);
	} else if (k == 3) {
		icon(ICON_CHECKMARK_CIRCLE_FILL_52, CARD_X + CARD_W - 80, cy, BLUEC);
	}
	if (i + 1 < page_rows(p) && i != sel && i + 1 != sel)
		fill_rect(CARD_X + 180, y + ROW_H - 2, CARD_W - 240, 2, LINE);
}

static void draw_top(int p)
{
	fill_circle(110, 130, 62, CARD);
	if (p == P_MAIN)
		icon(ICON_GLOBE_60, 110, 130, INK);
	else
		icon(ICON_CHEVRON_LEFT_44, 105, 130, INK);
	fill_circle(970, 130, 62, CARD);
	icon(ICON_POWER_60, 970, 130, REDC);
}

static void fill_ellipse(int cx, int cy, int rx, int ry, uint32_t c)
{
	for (int j = -ry; j <= ry; ++j) {
		int d = 0;
		while ((d + 1) * (d + 1) * ry * ry + j * j * rx * rx <= rx * rx * ry * ry)
			++d;
		fill_rect(cx - d, cy + j, 2 * d + 1, 1, c);
	}
}

static void thick_line(int x0, int y0, int x1, int y1, int r, uint32_t c)
{
	for (int k = 0; k <= 40; ++k)
		fill_circle(x0 + (x1 - x0) * k / 40, y0 + (y1 - y0) * k / 40, r, c);
}

/* Mitlk: the mascot, as a dot-matrix picture (green LEDs on a dark board): an Android-like head with two antennae, cartoon
 * eyes and arms, a round body and no legs. Same shape as the one in FiMiatoll (renderer/mitlk.js). */
#define MIT_W	44
#define MIT_H	46

static int in_ell(int x, int y, int cx2, int cy2, int rx2, int ry2)	/* all in half cells */
{
	int dx = 2 * x + 1 - cx2, dy = 2 * y + 1 - cy2;
	return dx * dx * ry2 * ry2 + dy * dy * rx2 * rx2 <= rx2 * rx2 * ry2 * ry2;
}

static int near_seg(int x, int y, int x0, int y0, int x1, int y1)
{
	int px = 2 * x + 1, py = 2 * y + 1, ax = 2 * x0, ay = 2 * y0, bx = 2 * x1, by = 2 * y1;
	int vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
	int t = vx * wx + vy * wy, l = vx * vx + vy * vy;
	if (t < 0) t = 0;
	if (t > l) t = l;
	int qx = ax + vx * t / l, qy = ay + vy * t / l;
	return (px - qx) * (px - qx) + (py - qy) * (py - qy) <= 5;
}

/* 0 off, 1 body, 2 dark (eyes, mouth), 3 eye shine */
static int mit_px(int x, int y)
{
	if ((x == 17 && y == 13) || (x == 26 && y == 13))
		return 3;
	if ((x >= 16 && x <= 18 && y >= 13 && y <= 17) || (x >= 25 && x <= 27 && y >= 13 && y <= 17))
		return 2;
	if ((y == 20 && x >= 20 && x <= 23) || (y == 19 && (x == 19 || x == 24)))
		return 2;
	if (in_ell(x, y, 44, 31, 26, 24))				/* head */
		return 1;
	if (near_seg(x, y, 14, 8, 10, 1) || near_seg(x, y, 30, 8, 34, 1))	/* antennae */
		return 1;
	if (x >= 13 && x <= 30 && y >= 27 && y <= 40)			/* body */
		return 1;
	if (in_ell(x, y, 44, 78, 18, 12))				/* round bottom */
		return 1;
	if (((x >= 8 && x <= 11) || (x >= 32 && x <= 35)) && y >= 28 && y <= 38)	/* arms */
		return 1;
	return 0;
}

static void draw_mitlk(int cx, int top)
{
	const int cell = 12, x0 = cx - MIT_W * cell / 2;
	static const uint32_t shade[4] = { 0xff9bffc4U, 0xff3ddc84U, 0xff2ab56aU, 0xff1d8a50U };
	fill_round(x0 - 36, top - 30, MIT_W * cell + 72, MIT_H * cell + 60, 60, 0xff10171aU);
	for (int gy = 0; gy < MIT_H; ++gy)
		for (int gx = 0; gx < MIT_W; ++gx) {
			int v = mit_px(gx, gy), t = (gx + gy) * 100 / (MIT_W + MIT_H);
			uint32_t c = v == 0 ? 0xff1a2428U : v == 2 ? 0xff0a0f11U : v == 3 ? 0xffd6ffe6U :
			    shade[t < 30 ? 0 : t < 52 ? 1 : t < 74 ? 2 : 3];
			fill_circle(x0 + gx * cell + cell / 2, top + gy * cell + cell / 2, v == 0 ? 2 : 5, c);
		}
}

static void draw_page(int p, int sel)
{
	fill_rect(0, 0, FB_W, FB_H, BG);
	draw_top(p);
	text(CARD_X, 300, 6, page_title(p), INK, 1);
	const char *s = page_sub(p);
	/* subtitle: up to two lines of 27 characters, broken at a space */
	char line[96];
	int n = 0, y = 440;
	const char *q = s;
	while (*q && y < 600) {
		int w = 0, brk = -1;
		const char *a = q;
		while (*a && text_w_n(q, a) < CARD_W) {
			if (*a == ' ')
				brk = (int)(a - q);
			next_cp(&a);
			++w;
		}
		if (*a && brk > 0)
			a = q + brk;
		n = 0;
		for (const char *b = q; b < a && n < 94; ++b)
			line[n++] = *b;
		line[n] = 0;
		text(CARD_X, y, 4, line, GREY, 0);
		y += 72;
		q = a;
		while (*q == ' ')
			++q;
	}
	if (p == P_ABOUT)
		draw_mitlk(540, 620);
	int rows = page_rows(p);
	fill_round(CARD_X, card_y(p), CARD_W, rows * ROW_H, 54, CARD);
	for (int i = 0; i < rows; ++i)
		draw_row(p, i, sel);
	text(FB_W / 2 - text_w("iMiatoll UEFI  -  XNU 8796  -  iOS 16.7", 3) / 2, 2250, 3, "iMiatoll UEFI  -  XNU 8796  -  iOS 16.7", GREY, 0);
}

/* ------------------------------------------------------------------ tests */

static int idle_exit(uint64_t *deadline) { return cnt() > *deadline; }

static void back_button(void)
{
	fill_round(40, 40, 300, 120, 40, CARD);
	icon(ICON_CHEVRON_LEFT_44, 90, 100, INK);
	text(130, 62, 4, back_text(), INK, 1);
}

static int in_back(int x, int y) { return x < 360 && y < 200; }

static void test_touch(void)
{
	fill_rect(0, 0, FB_W, FB_H, BG);
	back_button();
	text(CARD_X, 260, 5, row_title(P_TEST, 0), INK, 1);
	text(CARD_X, 380, 3, has_touch ? L6("Draw on the screen.", "Рисуйте по экрану.", "Малюйте по екрану.", "Zeichne auf dem Bildschirm.",
	    "Dibuja en la pantalla.", "Dessine sur l'écran.") :
	    L6("The touch controller did not answer.", "Тач не отвечает.", "Тач не відповідає.", "Der Touch-Controller antwortet nicht.",
	    "El controlador táctil no responde.", "Le contrôleur tactile ne répond pas."), GREY, 0);
	uint64_t dl = cnt() + frq() * 40;
	int errs = 0;
	while (has_touch && !idle_exit(&dl)) {
		int x = 0, y = 0, r = touch_poll(&x, &y);
		if (r < 0) {
			if (++errs > 400)
				break;
		} else {
			errs = 0;
			if (r > 0) {
				dl = cnt() + frq() * 40;
				if (in_back(x, y))
					return;
				fill_circle(x, y, 22, REDC);
			}
		}
		if (key_event() > 0)
			return;
		wait_us(4000);
	}
	uint64_t end = cnt() + frq() * 4;
	while (cnt() < end) {
		int x, y;
		if (has_touch && touch_poll(&x, &y) > 0 && in_back(x, y))
			return;
		if (key_event() > 0)
			return;
		wait_us(10000);
	}
}

static void box(int x, int y, int w, int h, const char *s, int on)
{
	fill_round(x, y, w, h, 40, on ? GREENC : CARD);
	text(x + w / 2 - text_w(s, 4) / 2, y + h / 2 - 36, 4, s, on ? WHITEC : INK, 1);
}

static void test_buttons(void)
{
	fill_rect(0, 0, FB_W, FB_H, BG);
	back_button();
	text(CARD_X, 260, 5, row_title(P_TEST, 1), INK, 1);
	text(CARD_X, 380, 3, L6("Press the keys. Back: tap the corner.", "Нажимайте клавиши. Выход: коснитесь угла.",
	    "Натискайте клавіші. Вихід: торкніться кута.", "Tasten drücken. Zurück: Ecke antippen.",
	    "Pulsa las teclas. Atrás: toca la esquina.", "Appuie sur les touches. Retour : touche le coin."), GREY, 0);
	uint64_t dl = cnt() + frq() * 40;
	int last[3] = { -1, -1, -1 };
	while (!idle_exit(&dl)) {
		int lv[3] = { 0, 0, 0 };
		if (key_levels(lv)) {
			for (int k = 0; k < 3; ++k)
				if (lv[k] != last[k]) {
					last[k] = lv[k];
					box(CARD_X, 560 + k * 300, CARD_W, 240, k == 0 ? "POWER" : k == 1 ? "VOL +" : "VOL -", lv[k]);
					dl = cnt() + frq() * 40;
				}
		}
		int x, y;
		if (has_touch && touch_poll(&x, &y) > 0 && in_back(x, y))
			return;
		wait_us(8000);
	}
}

static void test_display(void)
{
	static const uint32_t bars[8] = { 0xffff0000U, 0xff00ff00U, 0xff0000ffU, 0xffffffffU, 0xff000000U, 0xffffff00U, 0xff00ffffU, 0xffff00ffU };
	for (int k = 0; k < 8; ++k)
		fill_rect(0, k * (FB_H / 8), FB_W, FB_H / 8, bars[k]);
	for (int x = 0; x < FB_W; x += 8)
		fill_rect(x, FB_H - 160, 8, 160, 0xff000000U | ((uint32_t)(x * 255 / FB_W) * 0x010101U));
	uint64_t dl = cnt() + frq() * 30;
	while (!idle_exit(&dl)) {
		int x, y;
		if ((has_touch && touch_poll(&x, &y) > 0) || key_event() > 0)
			break;
		wait_us(10000);
	}
}

/* ------------------------------------------------------------------ navigation */

/* result of a page: row chosen, -2 globe, -3 power, -4 back corner, -1 timeout / dead inputs */
static int pick(int p, int *selp)
{
	int rows = page_rows(p), sel = *selp, cy0 = card_y(p);
	uint64_t deadline = cnt() + frq() * MENU_TIMEOUT_S;
	int was_down = 0, lx = 0, ly = 0;
	draw_page(p, sel);
	for (;;) {
		int e = key_event();
		if (e < 0 && !has_touch)
			return -1;
		if (e > 0) {
			deadline = cnt() + frq() * MENU_TIMEOUT_S;
			if (e == 1) {
				*selp = sel;
				return sel;
			}
			int old = sel;
			sel = (sel + (e == 3 ? 1 : rows - 1)) % rows;
			draw_row(p, old, sel);
			draw_row(p, sel, sel);
		}
		if (has_touch) {
			int x = 0, y = 0, r = touch_poll(&x, &y);
			if (r > 0) {
				was_down = 1;
				lx = x; ly = y;
				deadline = cnt() + frq() * MENU_TIMEOUT_S;
			} else if (r == 0 && was_down) {
				was_down = 0;
				*selp = sel;
				int dx = lx - 110, dy = ly - 130, ex = lx - 970;
				if (dx * dx + dy * dy < 90 * 90)
					return p == P_MAIN ? -2 : -4;
				if (ex * ex + dy * dy < 90 * 90)
					return -3;
				if (lx >= CARD_X && lx < CARD_X + CARD_W && ly >= cy0 && ly < cy0 + rows * ROW_H) {
					*selp = (ly - cy0) / ROW_H;
					return *selp;
				}
			}
		}
		if (cnt() > deadline)
			return -1;
		wait_us(6000);
	}
}

const char *menu_run(void)
{
	con_clear(BG);
	int keys = keys_init();
	fill_rect(0, 0, FB_W, FB_H, BG);
	text(CARD_X, 300, 6, "iMiatoll UEFI", INK, 1);
	text(CARD_X, 440, 4, "Loading touch...", GREY, 0);
	has_touch = touch_init();
	if (!keys && !has_touch)
		return "";
	int sel[5] = { 0, 0, 0, 0, 0 };
	int p = P_MAIN, back_to = P_MAIN;
	for (;;) {
		int c = pick(p, &sel[p]);
		if (c == -1) {
			build_args();
			return args;
		}
		if (c == -2) {
			back_to = p;
			p = P_LANG;
			sel[P_LANG] = lang;
			continue;
		}
		if (c == -3)
			menu_poweroff();
		if (c == -4) {
			p = p == P_TEST ? P_SET : p == P_LANG ? back_to : P_MAIN;
			continue;
		}
		if (p == P_MAIN) {
			if (c == 0) {
				build_args();
				return args;
			}
			if (c == 1)
				p = P_SET;
			else if (c == 2)
				menu_android();
			else if (c == 3)
				p = P_ABOUT;
			else if (c == 4)
				menu_poweroff();
		} else if (p == P_SET) {
			if (c < 5)
				opt[c] = !opt[c];
			else if (c == 5)
				p = P_TEST;
			else
				p = P_MAIN;
		} else if (p == P_LANG) {
			if (c < NLANG)
				lang = c;
			else
				p = back_to;
		} else if (p == P_ABOUT) {
			if (c == 4)
				p = P_MAIN;
		} else {
			if (c == 0)
				test_touch();
			else if (c == 1)
				test_buttons();
			else if (c == 2)
				test_display();
			else if (c == 7)
				p = P_SET;
		}
	}
}
