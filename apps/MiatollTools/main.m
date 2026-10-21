// MiatollTools: hardware diagnostics + IPA installer for iOS 16 on Redmi Note 9S (miatoll).
// Built with tools in apps/MiatollTools/build.sh (arm64, ad-hoc signed, setuid root on the image).
#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <zlib.h>
#import <dlfcn.h>
#import <objc/message.h>
#import <ifaddrs.h>
#import <arpa/inet.h>
#import <net/if.h>
#import <netdb.h>
#import <sys/sysctl.h>
#import <sys/mount.h>
#import <sys/stat.h>
#import <unistd.h>
#import <mach/mach.h>

static NSString *Sysctl(const char *name) {
	size_t n = 0; if (sysctlbyname(name, NULL, &n, NULL, 0) || !n) return @"?";
	char *b = calloc(1, n + 1); sysctlbyname(name, b, &n, NULL, 0);
	NSString *s = [NSString stringWithUTF8String:b]; free(b); return s ?: @"?";
}
static uint64_t SysctlU64(const char *name) { uint64_t v = 0; size_t n = sizeof(v); if (sysctlbyname(name, &v, &n, NULL, 0)) { uint32_t w = 0; n = sizeof(w); sysctlbyname(name, &w, &n, NULL, 0); return w; } return n == 4 ? (uint32_t)v : v; }

#pragma mark - text screens

@interface TextVC : UIViewController
@property (nonatomic, copy) NSString *(^producer)(void);
@property (nonatomic, strong) UITextView *tv;
@end
@implementation TextVC
- (void)viewDidLoad {
	[super viewDidLoad];
	self.view.backgroundColor = UIColor.systemBackgroundColor;
	self.tv = [[UITextView alloc] initWithFrame:self.view.bounds];
	self.tv.editable = NO; self.tv.font = [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightRegular];
	self.tv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	[self.view addSubview:self.tv];
	self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemRefresh target:self action:@selector(refresh)];
	[self refresh];
}
- (void)refresh { self.tv.text = @"..."; dispatch_async(dispatch_get_global_queue(0, 0), ^{ NSString *s = self.producer(); dispatch_async(dispatch_get_main_queue(), ^{ self.tv.text = s; }); }); }
@end

static NSString *InfoText(void) {
	NSMutableString *s = [NSMutableString string];
	UIDevice *d = UIDevice.currentDevice;
	[s appendFormat:@"Модель: %@ (%@)\niOS: %@  сборка %@\nЯдро: %@\n", Sysctl("hw.machine"), Sysctl("hw.target"), d.systemVersion, Sysctl("kern.osversion"), Sysctl("kern.version")];
	[s appendFormat:@"CPU: ядер %llu (активных %llu), частота %llu МГц\n", SysctlU64("hw.ncpu"), SysctlU64("hw.activecpu"), SysctlU64("hw.cpufrequency") / 1000000];
	[s appendFormat:@"Память: %.2f ГиБ\n", SysctlU64("hw.memsize") / 1073741824.0];
	struct timeval bt; size_t n = sizeof(bt); int mib[2] = {CTL_KERN, KERN_BOOTTIME};
	if (!sysctl(mib, 2, &bt, &n, NULL, 0)) { [s appendFormat:@"Время работы: %.0f мин\n", [[NSDate date] timeIntervalSince1970] / 60 - bt.tv_sec / 60.0]; }
	[s appendFormat:@"Экран: %.0fx%.0f pt, scale %.0f (%.0fx%.0f px)\n", UIScreen.mainScreen.bounds.size.width, UIScreen.mainScreen.bounds.size.height, UIScreen.mainScreen.scale, UIScreen.mainScreen.nativeBounds.size.width, UIScreen.mainScreen.nativeBounds.size.height];
	struct statfs sf; if (!statfs("/", &sf)) { [s appendFormat:@"Диск /: %.1f из %.1f ГиБ свободно\n", sf.f_bavail * (double)sf.f_bsize / 1073741824.0, sf.f_blocks * (double)sf.f_bsize / 1073741824.0]; }
	[s appendFormat:@"uid=%d euid=%d\n", getuid(), geteuid()];
	d.batteryMonitoringEnabled = YES;
	[s appendFormat:@"Батарея: %.0f%% состояние %ld (статичная в этом порте)\n", d.batteryLevel * 100, (long)d.batteryState];
	[s appendString:@"\nСеть (интерфейсы):\n"];
	struct ifaddrs *ifa, *p; if (!getifaddrs(&ifa)) {
		for (p = ifa; p; p = p->ifa_next) if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET) {
			char b[64]; inet_ntop(AF_INET, &((struct sockaddr_in *)p->ifa_addr)->sin_addr, b, sizeof(b));
			[s appendFormat:@"  %s  %s  %@\n", p->ifa_name, b, (p->ifa_flags & IFF_UP) ? @"up" : @"down"];
		}
		freeifaddrs(ifa);
	}
	return s;
}

static NSString *NetText(void) {
	NSMutableString *s = [NSMutableString string];
	for (NSString *h in @[@"apple.com", @"cloudflare.com"]) {
		struct addrinfo hints = {0}, *res = NULL; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
		CFAbsoluteTime t = CFAbsoluteTimeGetCurrent();
		int e = getaddrinfo(h.UTF8String, "80", &hints, &res);
		if (!e && res) { char b[64]; inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, b, sizeof(b)); [s appendFormat:@"DNS %@ -> %s (%.0f мс)\n", h, b, (CFAbsoluteTimeGetCurrent() - t) * 1000]; freeaddrinfo(res); }
		else [s appendFormat:@"DNS %@ -> ОШИБКА %s\n", h, gai_strerror(e)];
	}
	NSURL *u = [NSURL URLWithString:@"http://captive.apple.com/hotspot-detect.html"];
	NSMutableURLRequest *r = [NSMutableURLRequest requestWithURL:u cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:8];
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	CFAbsoluteTime t = CFAbsoluteTimeGetCurrent();
	[[NSURLSession.sharedSession dataTaskWithRequest:r completionHandler:^(NSData *data, NSURLResponse *resp, NSError *err) {
		if (err) [s appendFormat:@"HTTP: ОШИБКА %@\n", err.localizedDescription];
		else [s appendFormat:@"HTTP %ld, %lu байт, %.0f мс\n", (long)[(NSHTTPURLResponse *)resp statusCode], (unsigned long)data.length, (CFAbsoluteTimeGetCurrent() - t) * 1000];
		dispatch_semaphore_signal(sem);
	}] resume];
	dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 12 * NSEC_PER_SEC));
	return s.length ? s : @"нет ответа";
}

static NSString *RegistryText(void) {
	void *io = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_NOW);
	NSMutableString *s = [NSMutableString string];
	if (!io) return @"IOKit недоступен";
	typedef int (*MatchF)(unsigned, CFDictionaryRef, unsigned *);
	typedef unsigned (*NextF)(unsigned); typedef int (*RelF)(unsigned);
	typedef CFDictionaryRef (*MakeF)(const char *);
	typedef int (*NameF)(unsigned, char *);
	MatchF match = dlsym(io, "IOServiceGetMatchingServices"); NextF next = dlsym(io, "IOIteratorNext"); RelF rel = dlsym(io, "IOObjectRelease");
	MakeF make = dlsym(io, "IOServiceMatching"); (void)0;
	if (!match || !next || !make) return @"IOKit: нет функций";
	NSArray *names = @[@"AppleCLCD", @"IOMobileFramebuffer", @"IOSurfaceRoot", @"AppleARMPlatform", @"IOHIDSystem", @"IOHIDEventService", @"AppleKeyStore", @"IOPMPowerSource", @"IONetworkInterface", @"IOAudioDevice", @"AppleCamIn", @"IO80211Controller", @"IOBluetoothHCIController", @"AppleBaseband", @"IOAccelerator"];
	for (NSString *n in names) {
		unsigned it = 0; CFDictionaryRef d = make(n.UTF8String);
		int cnt = 0; if (d && !match(0, d, &it)) { unsigned o; while ((o = next(it))) { cnt++; rel(o); } rel(it); }
		[s appendFormat:@"%-26@ %@\n", n, cnt ? [NSString stringWithFormat:@"есть (%d)", cnt] : @"НЕТ"];
	}
	return s;
}

#pragma mark - display and touch tests

@interface ColorVC : UIViewController @property (nonatomic) int i; @end
@implementation ColorVC
- (void)viewDidLoad { [super viewDidLoad]; self.view.backgroundColor = UIColor.blackColor; [self.view addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(tap)]]; UILabel *l = [[UILabel alloc] initWithFrame:CGRectMake(20, 80, 300, 40)]; l.text = @"Тап: следующий цвет"; l.textColor = UIColor.grayColor; l.tag = 77; [self.view addSubview:l]; }
- (void)tap { NSArray *c = @[UIColor.whiteColor, UIColor.redColor, UIColor.greenColor, UIColor.blueColor, UIColor.blackColor, UIColor.grayColor]; self.i = (self.i + 1) % c.count; self.view.backgroundColor = c[self.i]; [self.view viewWithTag:77].hidden = self.i != 0 && self.i != 4; }
@end

@interface TouchView : UIView @property (nonatomic, strong) NSMutableDictionary *pts; @property (nonatomic) int events; @property (nonatomic, strong) UILabel *lbl; @end
@implementation TouchView
- (instancetype)initWithFrame:(CGRect)f { self = [super initWithFrame:f]; self.multipleTouchEnabled = YES; self.backgroundColor = UIColor.blackColor; self.pts = [NSMutableDictionary dictionary]; self.lbl = [[UILabel alloc] initWithFrame:CGRectMake(10, 60, f.size.width - 20, 80)]; self.lbl.numberOfLines = 3; self.lbl.textColor = UIColor.whiteColor; [self addSubview:self.lbl]; return self; }
- (void)upd { self.lbl.text = [NSString stringWithFormat:@"Касаний сейчас: %lu\nСобытий: %d\nПроведите пальцами по экрану (до 10)", (unsigned long)self.pts.count, self.events]; [self setNeedsDisplay]; }
- (void)touchesBegan:(NSSet *)t withEvent:(UIEvent *)e { [self touchesMoved:t withEvent:e]; }
- (void)touchesMoved:(NSSet *)ts withEvent:(UIEvent *)e { for (UITouch *t in ts) self.pts[[NSValue valueWithNonretainedObject:t]] = [NSValue valueWithCGPoint:[t locationInView:self]]; self.events++; [self upd]; }
- (void)touchesEnded:(NSSet *)ts withEvent:(UIEvent *)e { for (UITouch *t in ts) [self.pts removeObjectForKey:[NSValue valueWithNonretainedObject:t]]; [self upd]; }
- (void)touchesCancelled:(NSSet *)ts withEvent:(UIEvent *)e { [self touchesEnded:ts withEvent:e]; }
- (void)drawRect:(CGRect)r { CGContextRef c = UIGraphicsGetCurrentContext(); int k = 0; for (NSValue *v in self.pts.allValues) { CGPoint p = v.CGPointValue; CGContextSetFillColorWithColor(c, [UIColor colorWithHue:(k++ % 10) / 10.0 saturation:1 brightness:1 alpha:0.8].CGColor); CGContextFillEllipseInRect(c, CGRectMake(p.x - 40, p.y - 40, 80, 80)); } }
@end
@interface TouchVC : UIViewController @end
@implementation TouchVC - (void)loadView { self.view = [[TouchView alloc] initWithFrame:UIScreen.mainScreen.bounds]; } @end

#pragma mark - unzip + installer

static BOOL Unzip(NSString *zip, NSString *dest, NSString **err) {
	NSData *d = [NSData dataWithContentsOfFile:zip options:NSDataReadingMappedIfSafe error:nil];
	const uint8_t *b = d.bytes; NSUInteger n = d.length;
	if (n < 22) { *err = @"слишком маленький файл"; return NO; }
	NSInteger e = -1; for (NSInteger i = n - 22; i >= 0 && i > (NSInteger)n - 66000; i--) if (b[i] == 0x50 && b[i+1] == 0x4b && b[i+2] == 5 && b[i+3] == 6) { e = i; break; }
	if (e < 0) { *err = @"это не zip/ipa"; return NO; }
	uint16_t cnt = *(uint16_t *)(b + e + 10); uint32_t cdo = *(uint32_t *)(b + e + 16);
	NSFileManager *fm = NSFileManager.defaultManager; NSUInteger p = cdo;
	for (int k = 0; k < cnt; k++) {
		if (p + 46 > n || *(uint32_t *)(b + p) != 0x02014b50) { *err = @"повреждён каталог zip"; return NO; }
		uint16_t meth = *(uint16_t *)(b + p + 10); uint32_t csz = *(uint32_t *)(b + p + 20), usz = *(uint32_t *)(b + p + 24);
		uint16_t nl = *(uint16_t *)(b + p + 28), el = *(uint16_t *)(b + p + 30), cl = *(uint16_t *)(b + p + 32); uint32_t lho = *(uint32_t *)(b + p + 42);
		uint32_t ext = *(uint32_t *)(b + p + 38);
		NSString *name = [[NSString alloc] initWithBytes:b + p + 46 length:nl encoding:NSUTF8StringEncoding];
		p += 46 + nl + el + cl;
		if (!name.length || [name containsString:@".."]) continue;
		NSString *out = [dest stringByAppendingPathComponent:name];
		if ([name hasSuffix:@"/"]) { [fm createDirectoryAtPath:out withIntermediateDirectories:YES attributes:nil error:nil]; continue; }
		[fm createDirectoryAtPath:out.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
		uint16_t lnl = *(uint16_t *)(b + lho + 26), lel = *(uint16_t *)(b + lho + 28); const uint8_t *src = b + lho + 30 + lnl + lel;
		NSMutableData *od = [NSMutableData dataWithLength:usz];
		if (meth == 0) memcpy(od.mutableBytes, src, usz);
		else if (meth == 8) { z_stream z = {0}; inflateInit2(&z, -15); z.next_in = (Bytef *)src; z.avail_in = csz; z.next_out = od.mutableBytes; z.avail_out = usz; int r = inflate(&z, Z_FINISH); inflateEnd(&z); if (r != Z_STREAM_END && usz) { *err = [NSString stringWithFormat:@"ошибка распаковки %@", name]; return NO; } }
		else { *err = @"неподдерживаемое сжатие"; return NO; }
		uint32_t mode = ext >> 16;
		if ((mode & 0xF000) == 0xA000) { [fm createSymbolicLinkAtPath:out withDestinationPath:[[NSString alloc] initWithData:od encoding:NSUTF8StringEncoding] error:nil]; }
		else { [od writeToFile:out atomically:NO]; chmod(out.UTF8String, (mode & 0777) ?: 0644); }
	}
	return YES;
}

@interface InstallVC : UIViewController <UITextFieldDelegate> @property (nonatomic, strong) UITextField *url; @property (nonatomic, strong) UITextView *log; @end
@implementation InstallVC
- (void)addLine:(NSString *)l { dispatch_async(dispatch_get_main_queue(), ^{ self.log.text = [self.log.text stringByAppendingFormat:@"%@\n", l]; [self.log scrollRangeToVisible:NSMakeRange(self.log.text.length, 0)]; }); }
- (void)viewDidLoad {
	[super viewDidLoad]; self.view.backgroundColor = UIColor.systemBackgroundColor;
	CGFloat w = self.view.bounds.size.width;
	self.url = [[UITextField alloc] initWithFrame:CGRectMake(12, 110, w - 24, 40)]; self.url.borderStyle = UITextBorderStyleRoundedRect; self.url.placeholder = @"https://…/app.ipa"; self.url.autocapitalizationType = UITextAutocapitalizationTypeNone; self.url.autocorrectionType = UITextAutocorrectionTypeNo; self.url.keyboardType = UIKeyboardTypeURL; self.url.delegate = self; [self.view addSubview:self.url];
	UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem]; b.frame = CGRectMake(12, 160, w - 24, 44); [b setTitle:@"Скачать и установить в /Applications" forState:UIControlStateNormal]; [b addTarget:self action:@selector(go) forControlEvents:UIControlEventTouchUpInside]; [self.view addSubview:b];
	self.log = [[UITextView alloc] initWithFrame:CGRectMake(12, 212, w - 24, self.view.bounds.size.height - 230)]; self.log.editable = NO; self.log.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightRegular]; self.log.text = [NSString stringWithFormat:@"Права: uid=%d euid=%d%@\nПодходят arm64-приложения для iOS 16 без камеры/GPS/Wi-Fi.\n", getuid(), geteuid(), geteuid() == 0 ? @" (root, можно писать в /Applications)" : @" (НЕ root: установка в /Applications не получится)"]; [self.view addSubview:self.log];
}
- (BOOL)textFieldShouldReturn:(UITextField *)t { [t resignFirstResponder]; return YES; }
- (void)go {
	NSURL *u = [NSURL URLWithString:self.url.text]; [self.url resignFirstResponder];
	if (!u) { [self addLine:@"неверная ссылка"]; return; }
	[self addLine:[NSString stringWithFormat:@"скачиваю %@ …", u]];
	[[NSURLSession.sharedSession downloadTaskWithURL:u completionHandler:^(NSURL *loc, NSURLResponse *resp, NSError *err) {
		if (err || !loc) { [self addLine:[NSString stringWithFormat:@"ошибка загрузки: %@", err.localizedDescription]]; return; }
		[self addLine:[NSString stringWithFormat:@"скачано, HTTP %ld", (long)[(NSHTTPURLResponse *)resp statusCode]]];
		NSString *tmp = [NSTemporaryDirectory() stringByAppendingPathComponent:@"ipa"]; NSFileManager *fm = NSFileManager.defaultManager; [fm removeItemAtPath:tmp error:nil];
		NSString *e = nil; if (!Unzip(loc.path, tmp, &e)) { [self addLine:[@"распаковка: " stringByAppendingString:e ?: @"?"]]; return; }
		NSArray *apps = [fm contentsOfDirectoryAtPath:[tmp stringByAppendingPathComponent:@"Payload"] error:nil]; NSString *app = nil; for (NSString *a in apps) if ([a hasSuffix:@".app"]) app = a;
		if (!app) { [self addLine:@"в архиве нет Payload/*.app"]; return; }
		NSString *dst = [@"/Applications" stringByAppendingPathComponent:app]; [fm removeItemAtPath:dst error:nil];
		NSError *ce = nil; if (![fm copyItemAtPath:[[tmp stringByAppendingPathComponent:@"Payload"] stringByAppendingPathComponent:app] toPath:dst error:&ce]) { [self addLine:[NSString stringWithFormat:@"не удалось записать в %@: %@", dst, ce.localizedDescription]]; return; }
		[self addLine:[NSString stringWithFormat:@"установлено: %@", dst]];
		BOOL reg = NO; Class W = NSClassFromString(@"LSApplicationWorkspace");
		if (W) { id ws = [W performSelector:NSSelectorFromString(@"defaultWorkspace")]; SEL s = NSSelectorFromString(@"registerApplication:"); if ([ws respondsToSelector:s]) { reg = ((BOOL(*)(id, SEL, NSURL *))objc_msgSend)(ws, s, [NSURL fileURLWithPath:dst]); } }
		if (reg) [self addLine:@"зарегистрировано в системе, иконка появится на экране"];
		else { [self addLine:@"регистрация не удалась: сбрасываю кэш LaunchServices, ПЕРЕЗАГРУЗИТЕ iOS"]; NSArray *g = [fm contentsOfDirectoryAtPath:@"/private/var/containers/Data/System" error:nil]; for (NSString *c in g) { NSString *cd = [NSString stringWithFormat:@"/private/var/containers/Data/System/%@/Library/Caches", c]; for (NSString *f in [fm contentsOfDirectoryAtPath:cd error:nil]) if ([f hasPrefix:@"com.apple.LaunchServices-"]) [fm removeItemAtPath:[cd stringByAppendingPathComponent:f] error:nil]; } }
	}] resume];
}
@end

#pragma mark - root menu

@interface MenuVC : UITableViewController @property (nonatomic, strong) NSArray *items; @end
@implementation MenuVC
- (void)viewDidLoad { [super viewDidLoad]; self.title = @"MiatollTools"; self.items = @[@"Система и устройство", @"Тест экрана (цвета)", @"Тест тача (мультитач)", @"Сеть (DNS/HTTP)", @"Реестр железа (IOKit)", @"Установка IPA по ссылке"]; }
- (NSInteger)tableView:(UITableView *)t numberOfRowsInSection:(NSInteger)s { return self.items.count; }
- (UITableViewCell *)tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)ip { UITableViewCell *c = [t dequeueReusableCellWithIdentifier:@"c"] ?: [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"c"]; c.textLabel.text = self.items[ip.row]; c.accessoryType = UITableViewCellAccessoryDisclosureIndicator; return c; }
- (void)tableView:(UITableView *)t didSelectRowAtIndexPath:(NSIndexPath *)ip {
	UIViewController *vc = nil; NSString *title = self.items[ip.row];
	switch (ip.row) {
		case 0: { TextVC *v = [TextVC new]; v.producer = ^{ return InfoText(); }; vc = v; break; }
		case 1: vc = [ColorVC new]; break;
		case 2: vc = [TouchVC new]; break;
		case 3: { TextVC *v = [TextVC new]; v.producer = ^{ return NetText(); }; vc = v; break; }
		case 4: { TextVC *v = [TextVC new]; v.producer = ^{ return RegistryText(); }; vc = v; break; }
		case 5: vc = [InstallVC new]; break;
	}
	vc.title = title; [self.navigationController pushViewController:vc animated:YES];
}
@end

@interface AppDelegate : UIResponder <UIApplicationDelegate> @property (nonatomic, strong) UIWindow *window; @end
@implementation AppDelegate
- (BOOL)application:(UIApplication *)a didFinishLaunchingWithOptions:(NSDictionary *)o {
	self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
	self.window.rootViewController = [[UINavigationController alloc] initWithRootViewController:[[MenuVC alloc] initWithStyle:UITableViewStyleInsetGrouped]];
	[self.window makeKeyAndVisible]; return YES;
}
@end

int main(int argc, char *argv[]) { @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(AppDelegate.class)); } }
