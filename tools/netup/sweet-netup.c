/*
 * sweet-netup: publish the USB Ethernet (ue0, kernel-configured 192.168.7.2/24) to configd.
 * ue0 is not an IOKit IONetworkInterface, so configd never builds a service for it and the
 * phone has no default route / DNS. IPMonitor accepts services that only exist under
 * State:/Network/Service/... (VPN clients do the same), so write them from here.
 * Build: tools/netup/build.sh (arm64 iOS, ad-hoc signed). Runs as a launchd daemon.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

typedef const void *Store;
static Store (*pCreate)(CFAllocatorRef, CFStringRef, void *, void *);
static Boolean (*pSet)(Store, CFStringRef, CFPropertyListRef);
static Boolean (*pNotify)(Store, CFStringRef);

#define S(x) CFSTR(x)

static CFArrayRef arr1(CFStringRef s) { const void *v[1] = { s }; return CFArrayCreate(NULL, v, 1, &kCFTypeArrayCallBacks); }

static CFDictionaryRef dict(int n, CFStringRef *k, const void **v)
{
	return CFDictionaryCreate(NULL, (const void **)k, v, n, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

static void publish(Store st, const char *ifname, const char *addr, const char *mask, const char *router, const char *dns1, const char *dns2)
{
	CFStringRef ifn = CFStringCreateWithCString(NULL, ifname, kCFStringEncodingUTF8);
	CFStringRef a = CFStringCreateWithCString(NULL, addr, kCFStringEncodingUTF8);
	CFStringRef m = CFStringCreateWithCString(NULL, mask, kCFStringEncodingUTF8);
	CFStringRef r = CFStringCreateWithCString(NULL, router, kCFStringEncodingUTF8);
	CFStringRef d1 = CFStringCreateWithCString(NULL, dns1, kCFStringEncodingUTF8);
	CFStringRef d2 = CFStringCreateWithCString(NULL, dns2, kCFStringEncodingUTF8);
	CFStringRef svc = S("SweetUSB");

	/* State:/Network/Interface/ue0/Link */
	CFStringRef lk[1] = { S("Active") };
	const void *lv[1] = { kCFBooleanTrue };
	CFDictionaryRef link = dict(1, lk, lv);
	CFStringRef key = CFStringCreateWithFormat(NULL, NULL, S("State:/Network/Interface/%@/Link"), ifn);
	pSet(st, key, link);
	CFRelease(key);

	/* State:/Network/Service/SweetUSB/IPv4 and State:/Network/Interface/ue0/IPv4 */
	const void *dv[2] = { d1, d2 };
	CFArrayRef dnsArr = CFArrayCreate(NULL, dv, 2, &kCFTypeArrayCallBacks);
	CFStringRef ik[6] = { S("Addresses"), S("SubnetMasks"), S("Router"), S("InterfaceName"), S("ConfirmedInterfaceName"), S("NetworkSignature") };
	const void *iv[6] = { arr1(a), arr1(m), r, ifn, ifn, S("SweetUSB") };
	CFDictionaryRef ip4 = dict(6, ik, iv);
	key = CFStringCreateWithFormat(NULL, NULL, S("State:/Network/Service/%@/IPv4"), svc);
	pSet(st, key, ip4);
	CFRelease(key);
	CFStringRef ik2[3] = { S("Addresses"), S("SubnetMasks"), S("InterfaceName") };
	const void *iv2[3] = { arr1(a), arr1(m), ifn };
	CFDictionaryRef ifip4 = dict(3, ik2, iv2);
	key = CFStringCreateWithFormat(NULL, NULL, S("State:/Network/Interface/%@/IPv4"), ifn);
	pSet(st, key, ifip4);
	CFRelease(key);

	/* State:/Network/Service/SweetUSB/DNS */
	CFStringRef dk[2] = { S("ServerAddresses"), S("InterfaceName") };
	const void *dvv[2] = { dnsArr, ifn };
	CFDictionaryRef dns = dict(2, dk, dvv);
	key = CFStringCreateWithFormat(NULL, NULL, S("State:/Network/Service/%@/DNS"), svc);
	pSet(st, key, dns);
	CFRelease(key);

	/* Setup:/Network/Service/SweetUSB (gives IPMonitor a rank and a type) */
	CFStringRef ek[3] = { S("DeviceName"), S("Type"), S("Hardware") };
	const void *ev[3] = { ifn, S("Ethernet"), S("Ethernet") };
	CFDictionaryRef eth = dict(3, ek, ev);
	CFStringRef sk[2] = { S("Interface"), S("UserDefinedName") };
	const void *sv[2] = { eth, S("USB Ethernet") };
	CFDictionaryRef setup = dict(2, sk, sv);
	key = CFStringCreateWithFormat(NULL, NULL, S("Setup:/Network/Service/%@"), svc);
	pSet(st, key, setup);
	CFRelease(key);

	/* kick IPMonitor */
	key = CFStringCreateWithFormat(NULL, NULL, S("State:/Network/Service/%@/IPv4"), svc);
	pNotify(st, key);
	CFRelease(key);
}

int main(void)
{
	void *h = dlopen("/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration", RTLD_NOW);
	if (!h) { fprintf(stderr, "SWEETNETUP: dlopen failed: %s\n", dlerror()); return 1; }
	pCreate = dlsym(h, "SCDynamicStoreCreate");
	pSet = dlsym(h, "SCDynamicStoreSetValue");
	pNotify = dlsym(h, "SCDynamicStoreNotifyValue");
	if (!pCreate || !pSet || !pNotify) { fprintf(stderr, "SWEETNETUP: missing SC symbols\n"); return 1; }
	for (unsigned n = 0;; n++) {
		Store st = pCreate(NULL, S("sweet-netup"), NULL, NULL);
		if (st) {
			publish(st, "ue0", "192.168.7.2", "255.255.255.0", "192.168.7.1", "1.1.1.1", "8.8.8.8");
			if (n < 3 || n % 20 == 0) {
				fprintf(stderr, "SWEETNETUP: published ue0 192.168.7.2 router 192.168.7.1 dns 1.1.1.1 8.8.8.8 (round %u)\n", n);
			}
			CFRelease(st);
		} else {
			fprintf(stderr, "SWEETNETUP: SCDynamicStoreCreate failed (round %u)\n", n);
		}
		sleep(n < 6 ? 5 : 30);
	}
}
