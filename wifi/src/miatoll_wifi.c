/*
 * Wi-Fi through the PC: en0 as iOS's Wi-Fi side wants it (HackiOS's net / Apple80211 / station layers), with a
 * virtual radio behind it. Settings -> Wi-Fi shows the network "Miatoll via PC" as joined; the frames go over the
 * USB cable (bsd/dev/sweet_usb.c) to the computer, which shares its internet (tools/sweet-usbnet-share-macos.sh).
 * There is no radio involved. When the cable or the computer is not there, frames are dropped.
 */
#include <kern/locks.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/kpi_interface.h>
#include <pexpert/pexpert.h>
#include <netinet/in.h>
#include <sys/errno.h>
#include <sys/kpi_mbuf.h>
#include <sys/kpi_socket.h>
#include <sys/redwood_net.h>
#include <sys/redwood_sta.h>
#include <sys/sockio.h>
#include <sys/systm.h>

#define FK_SSID "Miatoll via PC"

/* the USB side sets this when its endpoints are up; NULL: no cable, frames are dropped */
static bool (*fk_usb_tx)(mbuf_t packet, size_t length);

void
miatoll_wifi_set_usb(bool (*tx)(mbuf_t, size_t))
{
	fk_usb_tx = tx;
}

static bool
fk_transmit(mbuf_t packet, size_t length)
{
	bool (*tx)(mbuf_t, size_t) = fk_usb_tx;
	return tx != NULL && tx(packet, length);
}

/* ---- the virtual radio ---- */
static bool fk_on, fk_joined;
static struct redwood_sta_bss fk_bss;

static void
fk_make_bss(void)
{
	static const uint8_t bssid[6] = {0x02, 0x4d, 0x49, 0x41, 0x54, 0x10};
	static const uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24};
	uint8_t *e = fk_bss.elements;
	size_t n = 0;

	bzero(&fk_bss, sizeof(fk_bss));
	memcpy(fk_bss.bssid, bssid, 6);
	fk_bss.ssid_length = sizeof(FK_SSID) - 1;
	memcpy(fk_bss.ssid, FK_SSID, fk_bss.ssid_length);
	fk_bss.frequency = 2437;                /* channel 6 */
	fk_bss.rssi = -35;
	fk_bss.beacon_interval = 100;
	fk_bss.capability = 0x0401;             /* ESS, short slot: open */
	e[n++] = 0;
	e[n++] = fk_bss.ssid_length;
	memcpy(e + n, FK_SSID, fk_bss.ssid_length);
	n += fk_bss.ssid_length;
	e[n++] = 1;
	e[n++] = sizeof(rates);
	memcpy(e + n, rates, sizeof(rates));
	n += sizeof(rates);
	e[n++] = 3;                             /* DS parameter set: channel 6 */
	e[n++] = 1;
	e[n++] = 6;
	fk_bss.elements_length = (uint16_t)n;
}

static int
fk_power(bool on)
{
	fk_on = on;
	if (!on) {
		fk_joined = false;
	}
	return 0;
}

static int
fk_scan(const struct redwood_sta_scan *what)
{
	(void)what;
	return fk_on ? 0 : ENETDOWN;
}

static void
fk_heard(void (*each)(const struct redwood_sta_bss *bss, void *context), void *context)
{
	if (fk_on) {
		each(&fk_bss, context);
	}
}

static int
fk_join(const struct redwood_sta_join *what)
{
	/* any network that is asked for is joined, as long as it is ours */
	if (what->ssid_length != fk_bss.ssid_length || memcmp(what->ssid, fk_bss.ssid, fk_bss.ssid_length) != 0) {
		return ENOENT;
	}
	fk_joined = true;
	return 0;
}

static int
fk_leave(void)
{
	fk_joined = false;
	return 0;
}

static void
fk_state(struct redwood_sta_state *state)
{
	bzero(state, sizeof(*state));
	state->phase = !fk_on ? REDWOOD_STA_OFF : fk_joined ? REDWOOD_STA_JOINED : REDWOOD_STA_IDLE;
	if (fk_joined) {
		state->bss = fk_bss;
	}
}

static unsigned
fk_channels(struct redwood_sta_channel *channels, unsigned most)
{
	unsigned n = 0;

	for (unsigned ch = 1; ch <= 11 && n < most; ++ch) {
		channels[n].frequency = (uint16_t)(2407 + 5 * ch);
		channels[n].flags = 0;
		n++;
	}
	return n;
}

static struct redwood_sta_ops fk_ops = {
	.power = fk_power,
	.scan = fk_scan,
	.heard = fk_heard,
	.join = fk_join,
	.leave = fk_leave,
	.state = fk_state,
	.channels = fk_channels,
	.address = {0x02, 0x4d, 0x49, 0x41, 0x54, 0x01},
};

static struct redwood_net_link fk_link = {
	.name = "usb-wifi",
	.address = {0x02, 0x4d, 0x49, 0x41, 0x54, 0x01},
	.wireless = true,
	.up = true,
	.transmit = fk_transmit,
};

/* en0, 192.168.7.2/24: the computer's side is 192.168.7.1 */
static void
fk_address(void)
{
	ifnet_t ifp = NULL;
	socket_t so = NULL;
	errno_t err = ifnet_find_by_name("en0", &ifp);

	if (err == 0) {
		err = proto_plumb(PF_INET, ifp);
		ifnet_release(ifp);
	}
	if (err == 0) {
		err = sock_socket(PF_INET, SOCK_DGRAM, 0, NULL, NULL, &so);
	}
	if (err == 0) {
		struct ifaliasreq ifra;
		struct sockaddr_in *a;

		bzero(&ifra, sizeof(ifra));
		snprintf(ifra.ifra_name, sizeof(ifra.ifra_name), "en0");
		a = (struct sockaddr_in *)&ifra.ifra_addr;
		a->sin_len = sizeof(*a);
		a->sin_family = AF_INET;
		a->sin_addr.s_addr = htonl(0xc0a80702);
		a = (struct sockaddr_in *)&ifra.ifra_mask;
		a->sin_len = sizeof(*a);
		a->sin_family = AF_INET;
		a->sin_addr.s_addr = htonl(0xffffff00);
		a = (struct sockaddr_in *)&ifra.ifra_broadaddr;
		a->sin_len = sizeof(*a);
		a->sin_family = AF_INET;
		a->sin_addr.s_addr = htonl(0xc0a807ff);
		err = sock_ioctl(so, SIOCAIFADDR, &ifra);
		sock_close(so);
	}
	printf("MIATOLL WIFI: en0 192.168.7.2/24 %s (err %d)\n", err ? "NOT configured" : "up", err);
}

static void
fk_start_thread(void *arg, wait_result_t result)
{
	static struct redwood_sta_join join;

	(void)arg;
	(void)result;
	/* give configd and wifid time to find en0, then power the virtual radio and join the network */
	delay_for_interval(8, NSEC_PER_SEC);
	redwood_sta_ask_power(true);
	delay_for_interval(2, NSEC_PER_SEC);
	join.ssid_length = fk_bss.ssid_length;
	memcpy(join.ssid, fk_bss.ssid, fk_bss.ssid_length);
	join.security = REDWOOD_STA_OPEN;
	redwood_sta_ask_join_standing(&join);
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

static void
fk_boot_thread(void *arg, wait_result_t result)
{
	thread_t thread;

	(void)arg;
	(void)result;
	delay_for_interval(3, NSEC_PER_SEC);            /* the network stack is long up by now; configd is about to look */
	fk_make_bss();
	if (redwood_net_attach(&fk_link)) {
		fk_address();
		redwood_sta_attach(&fk_ops);
		if (kernel_thread_start(fk_start_thread, NULL, &thread) == KERN_SUCCESS) {
			thread_deallocate(thread);
		}
	}
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

void
miatoll_wifi_init(void)
{
	thread_t thread;
	int enabled = 1;

	(void)PE_parse_boot_argn("miatollwifi", &enabled, sizeof(enabled));
	if (enabled && kernel_thread_start(fk_boot_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
