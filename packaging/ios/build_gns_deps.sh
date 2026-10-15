#!/bin/sh
# Builds GameNetworkingSockets (GNS) as a static library, with what it needs
# (protobuf, and OpenSSL's libcrypto for its encryption), for the iOS app:
#
#   sh packaging/ios/build_gns_deps.sh <prefix>
#
# GNS is what upstream OpenJKDF2 plays multiplayer over. With it linked in, the
# iOS app can join games hosted by upstream desktop builds, as well as ENet
# ones. Then:  GNS_PREBUILT=<prefix> ./build_ios.sh
#
# <prefix> gets include/ and lib/ (libGameNetworkingSockets_s.a, libprotobuf.a,
# libcrypto.a). Everything is linked into the app itself: iOS apps can't load
# a library of their own at run time the way desktop builds load GNS.
#
#   GNS_DEPS_TARGET=host  builds the same for the machine it runs on instead
#                         (Linux or macOS), to test the static-link path there
#   GNS_DEPS_WORK=<dir>   where to build (default: <prefix>.work)
#   IOS_DEPLOYMENT_TARGET minimum iOS version (default 18.0, as the app)
#
# Sources: lib/protobuf (3.21.12) and lib/GameNetworkingSockets (1.4.1, with
# cmake_modules/GameNetworkingSockets_v1.4.1.patch, as upstream builds it), and
# an OpenSSL release downloaded and checked against its SHA-256. protoc is
# built for the machine running this, to generate GNS's message code.

set -e

PREFIX=$1
if [ -z "$PREFIX" ]; then
    echo "usage: $0 <prefix>" >&2
    exit 1
fi
mkdir -p "$PREFIX"
PREFIX=$(cd "$PREFIX" && pwd)
SRC=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${GNS_DEPS_WORK:-$PREFIX.work}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
TARGET=${GNS_DEPS_TARGET:-ios}
export IOS_DEPLOYMENT_TARGET=${IOS_DEPLOYMENT_TARGET:-18.0}

OPENSSL_VERSION=3.5.9
OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a

if command -v sysctl >/dev/null 2>&1 && sysctl -n hw.ncpu >/dev/null 2>&1; then
    JOBS=$(sysctl -n hw.ncpu)
else
    JOBS=$(nproc 2>/dev/null || echo 2)
fi

for d in "$SRC/lib/protobuf/src" "$SRC/lib/GameNetworkingSockets/src"; do
    if [ ! -d "$d" ]; then
        echo "Missing $d: run git submodule update --init lib/protobuf lib/GameNetworkingSockets" >&2
        exit 1
    fi
done

# (the macOS host build would otherwise pick up an iOS SDK, and the other way round)
unset SDKROOT MACOSX_DEPLOYMENT_TARGET CPLUS_INCLUDE_PATH C_INCLUDE_PATH CC CXX

case "$TARGET" in
    ios)
        TOOLCHAIN_ARGS="--toolchain $SRC/cmake_modules/toolchain_ios.cmake"
        ;;
    host)
        TOOLCHAIN_ARGS=""
        ;;
    *)
        echo "GNS_DEPS_TARGET must be ios or host" >&2
        exit 1
        ;;
esac

COMMON_ARGS="-DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_POSITION_INDEPENDENT_CODE=ON"

echo "== protoc, for this machine"
cmake -S "$SRC/lib/protobuf" -B "$WORK/protoc-host" $COMMON_ARGS \
    -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF -Dprotobuf_WITH_ZLIB=OFF \
    -Dprotobuf_BUILD_CONFORMANCE=OFF -Dprotobuf_BUILD_EXAMPLES=OFF -Dprotobuf_DISABLE_RTTI=ON
cmake --build "$WORK/protoc-host" --target protoc -j "$JOBS"
PROTOC="$WORK/protoc-host/protoc"
"$PROTOC" --version

echo "== libprotobuf ($TARGET)"
cmake -S "$SRC/lib/protobuf" -B "$WORK/protobuf" $TOOLCHAIN_ARGS $COMMON_ARGS \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
    -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF -Dprotobuf_WITH_ZLIB=OFF \
    -Dprotobuf_BUILD_CONFORMANCE=OFF -Dprotobuf_BUILD_EXAMPLES=OFF -Dprotobuf_DISABLE_RTTI=ON \
    -Dprotobuf_BUILD_PROTOC_BINARIES=OFF
cmake --build "$WORK/protobuf" --target libprotobuf -j "$JOBS"
mkdir -p "$PREFIX/lib" "$PREFIX/include"
cp "$WORK/protobuf/libprotobuf.a" "$PREFIX/lib/"
# (its headers: what install would copy, without needing every target built)
(cd "$SRC/lib/protobuf/src" && find google -name '*.h' -o -name '*.inc' | while read -r f; do
    mkdir -p "$PREFIX/include/$(dirname "$f")"
    cp "$f" "$PREFIX/include/$f"
done)

echo "== OpenSSL $OPENSSL_VERSION libcrypto ($TARGET)"
TARBALL="$WORK/openssl-$OPENSSL_VERSION.tar.gz"
if [ ! -f "$TARBALL" ]; then
    curl -fsSL --retry 3 -o "$TARBALL.part" \
        "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz"
    mv "$TARBALL.part" "$TARBALL"
fi
if command -v shasum >/dev/null 2>&1; then
    SUM=$(shasum -a 256 "$TARBALL" | cut -d' ' -f1)
else
    SUM=$(sha256sum "$TARBALL" | cut -d' ' -f1)
fi
if [ "$SUM" != "$OPENSSL_SHA256" ]; then
    echo "OpenSSL download has the wrong SHA-256: $SUM" >&2
    rm -f "$TARBALL"
    exit 1
fi
rm -rf "$WORK/openssl"
mkdir -p "$WORK/openssl"
tar -xzf "$TARBALL" -C "$WORK/openssl" --strip-components=1
case "$TARGET" in
    ios)  OPENSSL_CONF_TARGET="ios64-xcrun -mios-version-min=$IOS_DEPLOYMENT_TARGET" ;;
    host) OPENSSL_CONF_TARGET="" ;;
esac
# (only the library GNS uses: AES-GCM, SHA-256, HMAC, X25519/Ed25519)
(cd "$WORK/openssl" && ./Configure $OPENSSL_CONF_TARGET \
    --prefix="$WORK/openssl-install" --libdir=lib \
    no-shared no-tests no-apps no-docs no-dso no-engine no-async no-module no-ui-console \
    no-comp no-ssl3 no-dtls no-srp no-psk no-legacy no-quic \
    && make -j "$JOBS" build_libs >/dev/null \
    && make install_dev >/dev/null)
cp "$WORK/openssl-install/lib/libcrypto.a" "$PREFIX/lib/"
rm -rf "$PREFIX/include/openssl"
cp -R "$WORK/openssl-install/include/openssl" "$PREFIX/include/openssl"

echo "== GameNetworkingSockets ($TARGET, static)"
rm -rf "$WORK/gns-src"
mkdir -p "$WORK/gns-src"
(cd "$SRC/lib/GameNetworkingSockets" && tar -cf - --exclude=.git .) | (cd "$WORK/gns-src" && tar -xf -)
(cd "$WORK/gns-src" && patch -p1 -s < "$SRC/cmake_modules/GameNetworkingSockets_v1.4.1.patch")
if [ "$TARGET" = host ] && [ "$(uname)" = Linux ]; then
    GNS_PLATFORM_ARGS="-DLINUX:BOOL=TRUE"
elif [ "$TARGET" = host ]; then
    GNS_PLATFORM_ARGS="-DIOS:BOOL=TRUE" # (macOS: upstream's way; the patch reads IOS as Apple)
else
    GNS_PLATFORM_ARGS=""
fi
cmake -S "$WORK/gns-src" -B "$WORK/gns" $TOOLCHAIN_ARGS $COMMON_ARGS \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_FIND_ROOT_PATH="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DBUILD_STATIC_LIB=ON -DBUILD_SHARED_LIB=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF -DBUILD_TOOLS=OFF \
    -DUSE_CRYPTO=OpenSSL -DENABLE_ICE=OFF -DUSE_STEAMWEBRTC=OFF \
    -DOPENSSL_USE_STATIC_LIBS=ON -DOPENSSL_ROOT_DIR="$PREFIX" \
    -DOPENSSL_INCLUDE_DIR="$PREFIX/include" -DOPENSSL_CRYPTO_LIBRARY="$PREFIX/lib/libcrypto.a" \
    -DProtobuf_USE_STATIC_LIBS=ON -DProtobuf_INCLUDE_DIR="$PREFIX/include" \
    -DProtobuf_LIBRARY="$PREFIX/lib/libprotobuf.a" -DProtobuf_LIBRARY_RELEASE="$PREFIX/lib/libprotobuf.a" \
    -DProtobuf_PROTOC_EXECUTABLE="$PROTOC" \
    $GNS_PLATFORM_ARGS
cmake --build "$WORK/gns" --target GameNetworkingSockets_s -j "$JOBS"
cp "$WORK/gns/src/libGameNetworkingSockets_s.a" "$PREFIX/lib/"
rm -rf "$PREFIX/include/GameNetworkingSockets"
mkdir -p "$PREFIX/include/GameNetworkingSockets"
cp -R "$WORK/gns-src/include/steam" "$PREFIX/include/GameNetworkingSockets/steam"

echo "== Done: $PREFIX"
ls -l "$PREFIX/lib"
