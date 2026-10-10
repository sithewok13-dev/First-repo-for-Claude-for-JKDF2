#!/bin/sh
# Makes the Mysteries of the Sith app from a built Jedi Knight app:
#
#   sh packaging/ios/make_mots_app.sh OpenJKDF2-iOS.app OpenJKDF2-MotS-iOS.app
#
# Both apps run the same engine binary. The copy gets its own name (OpenMoTS,
# upstream's name for its Android MotS launcher), its own icon
# (packaging/mots_icon-256.png) and its own bundle ID (the Jedi Knight one plus
# ".mots"). The bundle ID gives it a sandbox of its own: its own folder in the
# Files app, saves and settings. OpenJKDF2Game = mots in its Info.plist starts
# the engine as -motsCompat does (src/Platform/iOS/iosApp.m), and the engine
# then looks for the MotS data in Documents/mots.
#
# The Jedi Knight app is only read. The copy is signed again after the edits,
# inside-out like cmake_modules/target_ios_all.cmake does: ad-hoc, or with
# IOS_CODESIGN_IDENTITY. For a device build, IOS_TEAM_ID generates the
# entitlements for the MotS bundle ID (IOS_MOTS_ENTITLEMENTS overrides them),
# and IOS_MOTS_PROVISIONING_PROFILE replaces the profile copied from the Jedi
# Knight app, which only fits if it's a wildcard one. IOS_MOTS_BUNDLE_ID
# overrides the bundle ID.
#
# Needs macOS (plutil, codesign).

set -e

USAGE="usage: make_mots_app.sh <Jedi Knight .app> <new Mysteries of the Sith .app>"
SRC=${1:?$USAGE}
DST=${2:?$USAGE}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)

MOTS_NAME=OpenMoTS
IDENTITY=${IOS_CODESIGN_IDENTITY:--}

if [ ! -f "$SRC/Info.plist" ]; then
    echo "make_mots_app.sh: no $SRC/Info.plist" >&2
    exit 1
fi

JK_ID=$(plutil -extract CFBundleIdentifier raw -o - "$SRC/Info.plist")
MOTS_ID=${IOS_MOTS_BUNDLE_ID:-$JK_ID.mots}

rm -rf "$DST"
cp -R "$SRC" "$DST"
rm -rf "$DST/_CodeSignature"

PLIST="$DST/Info.plist"
plutil -replace CFBundleIdentifier -string "$MOTS_ID" "$PLIST"
plutil -replace CFBundleName -string "$MOTS_NAME" "$PLIST"
plutil -replace CFBundleDisplayName -string "$MOTS_NAME" "$PLIST"
plutil -replace OpenJKDF2Game -string mots "$PLIST"
plutil -lint "$PLIST"
# -replace also adds a key that isn't there yet; make sure it did.
GAME=$(plutil -extract OpenJKDF2Game raw -o - "$PLIST" || true)
NEW_ID=$(plutil -extract CFBundleIdentifier raw -o - "$PLIST" || true)
if [ "$GAME" != mots ] || [ "$NEW_ID" != "$MOTS_ID" ]; then
    echo "make_mots_app.sh: $PLIST was not updated" >&2
    exit 1
fi

ICON=$(plutil -extract CFBundleIconFile raw -o - "$PLIST")
cp "$ROOT/packaging/mots_icon-256.png" "$DST/$ICON"

if [ -n "$IOS_MOTS_PROVISIONING_PROFILE" ]; then
    cp "$IOS_MOTS_PROVISIONING_PROFILE" "$DST/embedded.mobileprovision"
fi

# The entitlements file stays outside the bundle, like the CMake build's.
ENTITLEMENTS=
GENERATED=
trap 'if [ -n "$GENERATED" ]; then rm -f "$GENERATED"; fi' EXIT
if [ -n "$IOS_MOTS_ENTITLEMENTS" ]; then
    ENTITLEMENTS=$IOS_MOTS_ENTITLEMENTS
elif [ -n "$IOS_TEAM_ID" ]; then
    GENERATED=$(mktemp)
    sed -e "s/@IOS_TEAM_ID@/$IOS_TEAM_ID/g" -e "s/@IOS_BUNDLE_ID@/$MOTS_ID/g" \
        "$ROOT/packaging/ios/OpenJKDF2.entitlements.in" > "$GENERATED"
    ENTITLEMENTS=$GENERATED
fi

# Sign inside-out: embedded code first, then the bundle.
for FRAMEWORK in "$DST"/Frameworks/*.framework; do
    if [ -d "$FRAMEWORK" ]; then
        codesign --force --sign "$IDENTITY" --timestamp=none "$FRAMEWORK"
    fi
done
if [ -n "$ENTITLEMENTS" ]; then
    codesign --force --sign "$IDENTITY" --timestamp=none --entitlements "$ENTITLEMENTS" "$DST"
else
    codesign --force --sign "$IDENTITY" --timestamp=none "$DST"
fi
codesign --verify --deep "$DST"

echo "Built $DST ($MOTS_NAME, $MOTS_ID) from $SRC ($JK_ID)"
