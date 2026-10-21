#!/bin/bash
# Build the configd plugin (arm64 iOS bundle, ad-hoc signed): tools/netup/EAPOLController
set -euo pipefail
cd "$(dirname "$0")"
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0 -isysroot "$SDK" -O2 -Wall -bundle \
	-framework CoreFoundation -o EAPOLController sweet-netplugin.c
codesign -s - --force EAPOLController 2>&1 | tail -1
file EAPOLController | cut -d: -f2
