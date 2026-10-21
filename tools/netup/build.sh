#!/bin/bash
# Build sweet-netup for arm64 iOS 16 and ad-hoc sign it. Output: tools/netup/sweet-netup
set -euo pipefail
cd "$(dirname "$0")"
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0 -isysroot "$SDK" -O2 -Wall \
	-framework CoreFoundation -o sweet-netup sweet-netup.c
codesign -s - --force sweet-netup 2>&1 | tail -1
file sweet-netup | cut -d: -f2
