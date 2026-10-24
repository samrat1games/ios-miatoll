#!/bin/bash
# libredwood-gl.dylib (HackiOS userland/src/eagl.c + gl_osmesa.c, APSL 2.0, in userland/gl/) on the Mesa of
# tools/mk-mesa.sh, both with install names in /private/var/miatoll/ and ad-hoc signed, into mesa-build/put/.
# Then: tools/sweet-put.py mesa-build/put/libredwood-gl.dylib mesa-build/put/libOSMesa.8.dylib
# The kernel (0147) inserts it into backboardd when the GPU is kept on (gpu=1).
set -e
cd "$(dirname "$0")/.."
M=mesa-build/out P=mesa-build/put
mkdir -p mesa-build/gl $P
python3 userland/gl/mkeagl.py userland/gl/eagl_slots.txt $M/gl.xml $M/exported-gl.txt userland/gl/eagl.c > mesa-build/gl/eagl_gen.h
cp $M/libOSMesa.8.dylib $P/
install_name_tool -id /private/var/miatoll/libOSMesa.8.dylib $P/libOSMesa.8.dylib 2>/dev/null
xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0 -O1 -dynamiclib \
	-install_name /private/var/miatoll/libredwood-gl.dylib -Imesa-build/gl -framework CoreFoundation \
	-framework IOKit -framework IOSurface -lobjc $P/libOSMesa.8.dylib -o $P/libredwood-gl.dylib \
	userland/gl/eagl.c userland/gl/gl_osmesa.c
codesign -f -s - $P/libOSMesa.8.dylib $P/libredwood-gl.dylib
ls -la $P
