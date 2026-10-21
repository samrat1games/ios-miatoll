#!/bin/bash
# Build SweetTools.app (arm64 iOS 16, ad-hoc signed with entitlements) into apps/SweetTools/build/SweetTools.app
set -euo pipefail
cd "$(dirname "$0")"
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
rm -rf build && mkdir -p build/SweetTools.app
xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0 -isysroot "$SDK" -fobjc-arc -O2 -Wall -Wno-deprecated-declarations \
	-framework UIKit -framework Foundation -framework CoreFoundation -framework CoreGraphics -lz -o build/SweetTools.app/SweetTools main.m
cp Info.plist build/SweetTools.app/Info.plist
codesign -s - --force --entitlements entitlements.plist build/SweetTools.app/SweetTools 2>&1 | tail -1
file build/SweetTools.app/SweetTools | cut -d: -f2
