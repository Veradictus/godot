#!/usr/bin/env bash
# =============================================================================
# Veradictus / Godot 4.7 export-template build script (macOS host).
#
# Builds export templates for every shipping platform from this Mac:
#   ios       - native (arm64 device + arm64 simulator)
#   macos     - native (x86_64 + arm64, lipo'd into universal)
#   windows   - cross-compiled with MinGW-w64
#   android   - cross-compiled with the Android SDK/NDK, packaged via Gradle
#   linux     - built in persistent OrbStack Ubuntu 22.04 machines (x86_64 + arm64)
#
# All platforms share the project flags: double precision, tools=no,
# build_profile=kaetram.build (3D disabled).
#
# Usage:
#   ./build_mac.sh                  # build everything, then package
#   ./build_mac.sh windows linux    # build only the listed platforms, then package
#   ./build_mac.sh package          # only (re)package whatever is already in bin/
#
# Every run finishes by renaming the built templates to Godot's export-template
# conventions and collecting them under bin/build_templates.
#
# Cross-compile toolchains live under ~/Projects/BuildTools (see that folder's
# README for how they were installed).
# =============================================================================
set -euo pipefail

GODOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$GODOT_DIR"

# --- Shared configuration ----------------------------------------------------
JOBS="$(sysctl -n hw.ncpu)"
PROFILE="kaetram.build"
PRECISION="double"
BUILD_TOOLS="$HOME/Projects/BuildTools"

# Architectures to build per platform (override here as needed).
WINDOWS_ARCHS=(x86_64)
ANDROID_ARCHS=(arm64 arm32 x86_64 x86)
LINUX_ARCHS=(x86_64 arm64)

# Android toolchain location.
export ANDROID_HOME="$BUILD_TOOLS/android-sdk"

# =============================================================================
# iOS
# =============================================================================
build_ios() {
    echo "==> iOS templates"
    scons platform=ios target=template_debug   tools=no build_profile="$PROFILE" arch=arm64 precision="$PRECISION"
    scons platform=ios target=template_release  tools=no build_profile="$PROFILE" arch=arm64 precision="$PRECISION"
    scons platform=ios target=template_debug   tools=no build_profile="$PROFILE" ios_simulator=yes arch=arm64 precision="$PRECISION"
    scons platform=ios target=template_release  tools=no build_profile="$PROFILE" ios_simulator=yes arch=arm64 precision="$PRECISION" generate_bundle=yes

    # Create simulator libraries.
    # cp -r misc/dist/ios_xcode ./bin

    # cp bin/libgodot.ios.template_debug.arm64.a bin/ios_xcode/libgodot.ios.debug.xcframework/ios-arm64/libgodot.a
    # lipo -create bin/libgodot.ios.template_debug.arm64.simulator.a bin/libgodot.ios.template_debug.x86_64.simulator.a -output bin/ios_xcode/libgodot.ios.debug.xcframework/ios-arm64_x86_64-simulator/libgodot.a

    # cp bin/libgodot.ios.template_release.arm64.a bin/ios_xcode/libgodot.ios.release.xcframework/ios-arm64/libgodot.a
    # lipo -create bin/libgodot.ios.template_release.arm64.simulator.a bin/libgodot.ios.template_release.x86_64.simulator.a -output bin/ios_xcode/libgodot.ios.release.xcframework/ios-arm64_x86_64-simulator/libgodot.a
}

# =============================================================================
# macOS
# =============================================================================
build_macos() {
    echo "==> macOS templates"
    scons platform=macos target=template_debug   tools=no build_profile="$PROFILE" arch=x86_64 precision="$PRECISION"
    scons platform=macos target=template_release  tools=no build_profile="$PROFILE" arch=x86_64 precision="$PRECISION"
    scons platform=macos target=template_debug   tools=no build_profile="$PROFILE" arch=arm64  precision="$PRECISION"
    scons platform=macos target=template_release  tools=no build_profile="$PROFILE" arch=arm64  precision="$PRECISION" generate_bundle=yes

    # # # Combine macOS templates.
    # lipo -create bin/godot.macos.template_release.x86_64 bin/godot.macos.template_release.arm64 -output bin/godot.macos.template_release.universal
    # lipo -create bin/godot.macos.template_debug.x86_64 bin/godot.macos.template_debug.arm64 -output bin/godot.macos.template_debug.universal

    # # Create the MacOS app bundle.
    # cp -r misc/dist/macos_template.app .
    # mkdir -p macos_template.app/Contents/MacOS
    # cp bin/godot.macos.template_release.universal macos_template.app/Contents/MacOS/godot_macos_release.universal
    # cp bin/godot.macos.template_debug.universal macos_template.app/Contents/MacOS/godot_macos_debug.universal
    # chmod +x macos_template.app/Contents/MacOS/godot_macos*

    # # Remove the old MacOS app bundle.
    # rm -rf bin/macos_template.app

    # mv macos_template.app bin/macos_template.app

    # cd bin

    # zip -q -9 -r macos.zip macos_template.app
}

# =============================================================================
# Windows (cross-compiled with MinGW-w64; install: brew install mingw-w64)
# d3d12=no because the kaetram.build profile disables 3D.
# =============================================================================
build_windows() {
    echo "==> Windows templates (${WINDOWS_ARCHS[*]})"
    for arch in "${WINDOWS_ARCHS[@]}"; do
        scons platform=windows target=template_debug   tools=no build_profile="$PROFILE" arch="$arch" precision="$PRECISION" use_mingw=yes d3d12=no -j"$JOBS"
        scons platform=windows target=template_release  tools=no build_profile="$PROFILE" arch="$arch" precision="$PRECISION" use_mingw=yes d3d12=no -j"$JOBS"
    done
}

# =============================================================================
# Android (cross-compiled with the SDK/NDK in ~/Projects/BuildTools, then the
# native libs are packaged into export templates with Gradle).
# Requires JDK 17 for the Gradle build (JDK 25 is too new for the AGP version).
# =============================================================================
build_android() {
    echo "==> Android templates (${ANDROID_ARCHS[*]})"
    local java17
    java17="$(/usr/libexec/java_home -v 17)"

    for arch in "${ANDROID_ARCHS[@]}"; do
        scons platform=android target=template_debug   tools=no build_profile="$PROFILE" arch="$arch" precision="$PRECISION" -j"$JOBS"
        scons platform=android target=template_release  tools=no build_profile="$PROFILE" arch="$arch" precision="$PRECISION" -j"$JOBS"
    done

    # Package the compiled .so libraries into the APK/AAB export templates.
    echo "==> Packaging Android templates with Gradle"
    ( cd platform/android/java && JAVA_HOME="$java17" ./gradlew generateGodotTemplates )
    # Outputs: bin/android_debug.apk, bin/android_release.apk,
    #          bin/android_dev.apk, bin/android_source.zip
}

# =============================================================================
# Linux (built inside persistent OrbStack Ubuntu 22.04 machines — see
# ~/Projects/BuildTools/README.md). One machine per architecture:
#   x86_64 -> godot-linux        (amd64; emulated on Apple Silicon)
#   arm64  -> godot-linux-arm64  (arm64; native, fast)
# Persistent machines keep the SCons object cache, so re-runs are incremental.
# OrbStack auto-mounts the Mac filesystem, so $GODOT_DIR is visible as-is.
# The machines are booted at the start of the Linux build and stopped again
# when it finishes (or aborts), so they don't linger running afterwards.
# =============================================================================
linux_machine_for_arch() {
    case "$1" in
        x86_64) echo "godot-linux" ;;
        arm64)  echo "godot-linux-arm64" ;;
        *) return 1 ;;
    esac
}

build_linux() {
    echo "==> Linux templates (${LINUX_ARCHS[*]})"
    # orb run does not inherit the host shell env, so the encryption key is
    # forwarded explicitly below. Fail fast rather than silently shipping
    # unencrypted Linux templates.
    if [ -z "${SCRIPT_AES256_ENCRYPTION_KEY:-}" ]; then
        echo "ERROR: SCRIPT_AES256_ENCRYPTION_KEY is not set; export it before building Linux." >&2
        exit 1
    fi

    # Resolve the machines needed for the requested arches.
    local machines=() arch machine
    for arch in "${LINUX_ARCHS[@]}"; do
        if ! machine="$(linux_machine_for_arch "$arch")"; then
            echo "ERROR: no OrbStack machine mapped for Linux arch '$arch'." >&2
            exit 1
        fi
        machines+=("$machine")
    done

    # Shut the build machines down when we leave this function — on success or
    # on a set -e abort mid-build — so they don't linger running afterwards.
    # Machine names are baked into the trap so it needs no surviving locals.
    local stop_cmd=""
    for machine in "${machines[@]}"; do
        stop_cmd+="echo '==> Stopping Linux machine $machine'; orb stop '$machine' >/dev/null 2>&1 || true; "
    done
    trap "$stop_cmd" EXIT

    # Boot the machines (also starts the OrbStack service and validates that the
    # machine exists). A failure here means the machine was never created.
    for machine in "${machines[@]}"; do
        echo "==> Booting Linux machine $machine"
        if ! orb start "$machine" >/dev/null 2>&1; then
            echo "ERROR: could not start OrbStack machine '$machine'; create it per ~/Projects/BuildTools/README.md." >&2
            exit 1
        fi
    done

    for arch in "${LINUX_ARCHS[@]}"; do
        machine="$(linux_machine_for_arch "$arch")"
        for target in template_debug template_release; do
            orb run -m "$machine" -w "$GODOT_DIR" \
                env SCRIPT_AES256_ENCRYPTION_KEY="$SCRIPT_AES256_ENCRYPTION_KEY" \
                scons platform=linuxbsd target="$target" tools=no \
                      build_profile="$PROFILE" precision="$PRECISION" \
                      arch="$arch" -j"$JOBS"
        done
    done

    # Stop the machines now and drop the trap so we don't stop twice at exit.
    eval "$stop_cmd"
    trap - EXIT
}

# =============================================================================
# Package: rename built templates to Godot's export-template conventions and
# collect them under bin/build_templates. Only files that exist are moved, so
# this works for partial builds too.
# =============================================================================
package_templates() {
    echo "==> Packaging templates into bin/build_templates"
    local out="$GODOT_DIR/bin/build_templates"
    mkdir -p "$out"
    cd "$GODOT_DIR/bin"

    # Move "$1" -> build_templates/"$2" when "$1" exists.
    _place() {
        if [ -e "$1" ]; then
            mv -f "$1" "$out/$2"
            echo "    $1 -> build_templates/$2"
        fi
    }

    # iOS / macOS bundles.
    _place godot_ios.zip   ios.zip
    _place godot_macos.zip macos.zip

    # Windows: debug/release, GUI + console (names per build_windows_double.sh).
    local arch
    for arch in "${WINDOWS_ARCHS[@]}"; do
        _place "godot.windows.template_debug.${arch}.exe"           "windows_debug_${arch}.exe"
        _place "godot.windows.template_debug.${arch}.console.exe"   "windows_debug_${arch}_console.exe"
        _place "godot.windows.template_release.${arch}.exe"         "windows_release_${arch}.exe"
        _place "godot.windows.template_release.${arch}.console.exe" "windows_release_${arch}_console.exe"
    done

    # Linux: godot.linuxbsd.template_<target>.<arch> -> linux_<target>.<arch>.
    for arch in "${LINUX_ARCHS[@]}"; do
        _place "godot.linuxbsd.template_debug.${arch}"   "linux_debug.${arch}"
        _place "godot.linuxbsd.template_release.${arch}" "linux_release.${arch}"
    done

    # Android (Gradle already emits Godot's expected names).
    _place android_debug.apk   android_debug.apk
    _place android_release.apk android_release.apk
    _place android_source.zip  android_source.zip

    cd "$GODOT_DIR"
}

# =============================================================================
# Dispatcher
# =============================================================================
PLATFORMS=("$@")
if [ ${#PLATFORMS[@]} -eq 0 ]; then
    PLATFORMS=(ios macos windows android linux)
fi

for platform in "${PLATFORMS[@]}"; do
    case "$platform" in
        ios)     build_ios ;;
        macos)   build_macos ;;
        windows) build_windows ;;
        android) build_android ;;
        linux)   build_linux ;;
        package) : ;;  # package-only run; packaging happens after this loop
        *) echo "Unknown platform: $platform" >&2; exit 1 ;;
    esac
done

package_templates

echo "==> Done. Templates are in: $GODOT_DIR/bin/build_templates"
