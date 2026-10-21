/*
 * configd plugin that publishes the USB Ethernet (ue0, kernel-configured 192.168.7.2/24) so IPMonitor
 * installs a default route and DNS. ue0 is not an IOKit IONetworkInterface, so configd builds no service
 * for it. launchd refuses (panics on) non-platform daemons, so this runs INSIDE configd: configd loads
 * only allow-listed bundle ids, hence the bundle masquerades as com.apple.SystemConfiguration.EAPOLController
 * (802.1X, unused here); tools/miatoll-live-tweaks.py swaps its Info.plist and executable.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

typedef const void *Store;
static Store (*pCreate)(CFAllocatorRef, CFStringRef, void *, void *);
static Boolean (*pSet)(Store, CFStringRef, CFPropertyListRef);
static Boolean (*pNotify)(Store, CFStringRef);
static int console = -1;
static unsigned rounds;

#define S(x) CFSTR(x)

static void say(const char *fmt, ...)
{
	char b[256];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	if (console >= 0 && n > 0) write(console, b, n < (int)sizeof(b) ? n : (int)sizeof(b) - 1);
}

static CFArrayRef arr1(CFStringRef s) { const void *v[1] = { s }; return CFArrayCreate(NULL, v, 1, &kCFTypeArrayCallBacks); }
static CFDictionaryRef dict(int n, CFStringRef *k, const void **v)
{
	return CFDictionaryCreate(NULL, (const void **)k, v, n, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

static void publish(Store st)
{
	CFStringRef ifn = S("ue0"), a = S("192.168.7.2"), m = S("255.255.255.0"), r = S("192.168.7.1");
	CFStringRef svc = S("SweetUSB"), key;

	CFStringRef lk[1] = { S("Active") };
	const void *lv[1] = { kCFBooleanTrue };
	pSet(st, S("State:/Network/Interface/ue0/Link"), dict(1, lk, lv));

	CFStringRef ik[6] = { S("Addresses"), S("SubnetMasks"), S("Router"), S("InterfaceName"), S("ConfirmedInterfaceName"), S("NetworkSignature") };
	const void *iv[6] = { arr1(a), arr1(m), r, ifn, ifn, S("SweetUSB") };
	pSet(st, S("State:/Network/Service/SweetUSB/IPv4"), dict(6, ik, iv));
	CFStringRef ik2[3] = { S("Addresses"), S("SubnetMasks"), S("InterfaceName") };
	const void *iv2[3] = { arr1(a), arr1(m), ifn };
	pSet(st, S("State:/Network/Interface/ue0/IPv4"), dict(3, ik2, iv2));

	const void *dv[2] = { S("1.1.1.1"), S("8.8.8.8") };
	CFStringRef dk[2] = { S("ServerAddresses"), S("InterfaceName") };
	const void *dvv[2] = { CFArrayCreate(NULL, dv, 2, &kCFTypeArrayCallBacks), ifn };
	pSet(st, S("State:/Network/Service/SweetUSB/DNS"), dict(2, dk, dvv));

	CFStringRef ek[3] = { S("DeviceName"), S("Type"), S("Hardware") };
	const void *ev[3] = { ifn, S("Ethernet"), S("Ethernet") };
	CFStringRef sk[2] = { S("Interface"), S("UserDefinedName") };
	const void *sv[2] = { dict(3, ek, ev), S("USB Ethernet") };
	pSet(st, S("Setup:/Network/Service/SweetUSB"), dict(2, sk, sv));

	(void)svc; (void)key;
	pNotify(st, S("State:/Network/Service/SweetUSB/IPv4"));
}

static void tick(CFRunLoopTimerRef t, void *info)
{
	Store st = pCreate(NULL, S("sweet-netplugin"), NULL, NULL);
	if (st) {
		publish(st);
		if (rounds < 3 || rounds % 20 == 0) say("SWEETNETPLUGIN: published ue0 192.168.7.2 router 192.168.7.1 (round %u)\n", rounds);
		CFRelease(st);
	} else {
		say("SWEETNETPLUGIN: SCDynamicStoreCreate failed (round %u)\n", rounds);
	}
	rounds++;
}

__attribute__((visibility("default"))) void load(CFBundleRef bundle, Boolean verbose)
{
	console = open("/dev/console", O_WRONLY | O_NOCTTY);
	say("SWEETNETPLUGIN: load\n");
}

__attribute__((visibility("default"))) void start(const char *bundleName, const char *bundleDir, Boolean verbose)
{
	pCreate = dlsym(RTLD_DEFAULT, "SCDynamicStoreCreate");
	pSet = dlsym(RTLD_DEFAULT, "SCDynamicStoreSetValue");
	pNotify = dlsym(RTLD_DEFAULT, "SCDynamicStoreNotifyValue");
	if (!pCreate || !pSet || !pNotify) { say("SWEETNETPLUGIN: missing SC symbols\n"); return; }
	CFRunLoopTimerContext ctx = { 0 };
	CFRunLoopTimerRef t = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 3, 15, 0, 0, tick, &ctx);
	CFRunLoopAddTimer(CFRunLoopGetCurrent(), t, kCFRunLoopCommonModes);
	say("SWEETNETPLUGIN: start, timer armed\n");
}
