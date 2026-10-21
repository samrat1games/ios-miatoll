#!/bin/bash
# Build MiatollTools.app (arm64 iOS 16, ad-hoc signed with entitlements) into apps/MiatollTools/build/MiatollTools.app
set -euo pipefail
cd "$(dirname "$0")"
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
rm -rf build && mkdir -p build/MiatollTools.app
xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0 -isysroot "$SDK" -fobjc-arc -O2 -Wall -Wno-deprecated-declarations \
	-framework UIKit -framework Foundation -framework CoreFoundation -framework CoreGraphics -lz -o build/MiatollTools.app/MiatollTools main.m
cp Info.plist build/MiatollTools.app/Info.plist
codesign -s - --force --entitlements entitlements.plist build/MiatollTools.app/MiatollTools 2>&1 | tail -1
file build/MiatollTools.app/MiatollTools | cut -d: -f2
