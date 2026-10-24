#!/bin/bash
# Mesa (freedreno, OSMesa) for iOS 16 arm64 on macOS, for the Adreno 618 via the kernel's RedwoodGPU
# service (patch 0145). Adapted from HackiOS (qdizax-w/hackios-xnu-userland, branch miatoll,
# tools/mk-mesa.sh; tools/mesa/* is theirs: freedreno-darwin.patch, redwood-drm = libdrm on IOKit,
# air/ = Metal AIR -> NIR). Needs Xcode (iPhoneOS SDK), meson, ninja, python3.
#   tools/mk-mesa.sh            -> mesa-build/out/libOSMesa.8.dylib (install name /redwood/lib/...)
#   AIR=1 tools/mk-mesa.sh      also libredwoodair.dylib
set -e
VERSION=25.0.7
SHA256=592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=$ROOT/mesa-build
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
CC=$(xcrun --sdk iphoneos -f clang)
MIN=-miphoneos-version-min=16.0
mkdir -p "$B/out"
cd "$B"
PATCHES=$(cat "$ROOT"/tools/mesa/*.patch | shasum -a 256 | cut -d' ' -f1)
if [ ! -d mesa-$VERSION ] || [ "$(cat mesa-$VERSION/.redwood-patches 2>/dev/null)" != "$PATCHES" ]; then
	[ -f mesa-$VERSION.tar.xz ] || curl -sL --fail -o mesa-$VERSION.tar.xz https://archive.mesa3d.org/mesa-$VERSION.tar.xz
	echo "$SHA256  mesa-$VERSION.tar.xz" | shasum -a 256 -c --quiet
	rm -rf mesa-$VERSION build-*
	tar xJf mesa-$VERSION.tar.xz
	for p in "$ROOT"/tools/mesa/*.patch; do patch -s -d mesa-$VERSION -p1 < "$p"; done
	echo "$PATCHES" > mesa-$VERSION/.redwood-patches
fi
rm -rf mesa-$VERSION/src/redwood-drm mesa-$VERSION/src/compiler/air
cp -R "$ROOT/tools/mesa/redwood-drm" mesa-$VERSION/src/redwood-drm
cp -R "$ROOT/tools/mesa/air" mesa-$VERSION/src/compiler/air
if [ ! -x venv/bin/python3 ]; then
	python3 -m venv venv
	venv/bin/pip -q install mako pyyaml packaging
fi
export PATH=$B/venv/bin:/opt/homebrew/opt/bison/bin:$PATH   # (bison 3: macOS has 2.3)
cat > cross.ini <<INI
[binaries]
c = ['$CC', '-arch', 'arm64', '-isysroot', '$SDK', '$MIN']
cpp = ['$CC++', '-arch', 'arm64', '-isysroot', '$SDK', '$MIN']
objc = ['$CC', '-arch', 'arm64', '-isysroot', '$SDK', '$MIN']
ar = '$(xcrun -f ar)'
strip = '$(xcrun -f strip)'
pkg-config = 'false'
[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'
[properties]
needs_exe_wrapper = true
INI
DRIVERS=${DRIVERS:-softpipe,freedreno}
BUILD=build-${DRIVERS//,/-}
[ -f $BUILD/build.ninja ] || meson setup $BUILD mesa-$VERSION --cross-file cross.ini --wrap-mode=nofallback \
	--prefix=/redwood --libdir=lib --buildtype=release -Db_ndebug=true \
	-Dplatforms= -Dgallium-drivers="$DRIVERS" -Dfreedreno-kmds=msm -Dvulkan-drivers= -Dglx=disabled -Degl=disabled -Dgbm=disabled \
	-Dosmesa=true -Dllvm=disabled -Dshared-glapi=enabled -Dgles1=disabled -Dgles2=enabled -Dopengl=true \
	-Dzstd=disabled -Dxmlconfig=disabled -Dexpat=disabled -Dshader-cache=disabled -Dvalgrind=disabled \
	-Dlibunwind=disabled -Dglvnd=disabled -Dvideo-codecs= -Dtools= -Dgallium-vdpau=disabled -Dgallium-va=disabled \
	-Dgallium-xa=disabled -Dlmsensors=disabled -Dbuild-tests=false > meson-setup.log
ninja -C $BUILD > ninja.log
if [ "${AIR:-}" = 1 ]; then
	ninja -C $BUILD src/gallium/drivers/freedreno/libredwoodair.dylib >> ninja.log
	strip -x -o out/libredwoodair.dylib $BUILD/src/gallium/drivers/freedreno/libredwoodair.dylib
	install_name_tool -id /redwood/lib/libredwoodair.dylib out/libredwoodair.dylib
fi
strip -x -o out/libOSMesa.8.dylib $BUILD/src/gallium/targets/osmesa/libOSMesa.8.dylib
install_name_tool -id /redwood/lib/libOSMesa.8.dylib out/libOSMesa.8.dylib
nm -gU out/libOSMesa.8.dylib | awk '$3 ~ /^_gl/ { print substr($3, 2) }' > out/exported-gl.txt
cp mesa-$VERSION/src/mapi/glapi/registry/gl.xml out/gl.xml
echo "mk-mesa.sh: Mesa $VERSION ($DRIVERS): $B/out/libOSMesa.8.dylib, $(stat -f %z out/libOSMesa.8.dylib) bytes," \
	"$(wc -l < out/exported-gl.txt) gl* functions"
