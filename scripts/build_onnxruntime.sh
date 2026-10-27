#!/usr/bin/env bash
# Build a small, static, CPU-only ONNX Runtime for the products that run neural networks.
#
#   scripts/build_onnxruntime.sh <arch> <operators.config> [out-dir]
#
# Only the operators listed in the config (see products/dialogue/models/README.md) are compiled in, and everything
# is combined into one libonnxruntime.a, so the plug-in carries no dynamic library. The result is installed to out-dir (default build/deps/onnxruntime-<ver>-<arch>)
# as include/ and lib/libonnxruntime.a, and is skipped when that already exists for the same config.
#
# Needs: Python >= 3.10 (PYTHON, default python3), CMake, Ninja, a C++20 compiler; macOS only for now.
set -euo pipefail

ORT_VERSION="1.28.0"
ORT_SHA256="9616cbdbbfcb1420b3261cd280a047d74ab0a249825e577b0e2dd310e22f6b83"
FLATBUFFERS_VERSION="25.12.19" # Python package the operator reduction needs

arch="${1:?usage: build_onnxruntime.sh <arm64|x86_64> <operators.config> [out-dir]}"
config="$(cd "$(dirname "${2:?operators config required}")" && pwd)/$(basename "$2")"
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${3:-$root/build/deps/onnxruntime-$ORT_VERSION-$arch}"
python="${PYTHON:-python3}"
deploy_target="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
[[ "$deploy_target" == *.* ]] || deploy_target="$deploy_target.0"

stamp="$out/.config-$(shasum -a 256 "$config" | cut -c1-16)-$deploy_target"
if [[ -f "$stamp" && -f "$out/lib/libonnxruntime.a" ]]; then
    echo "onnxruntime $ORT_VERSION ($arch) is up to date in $out"
    exit 0
fi

src_root="$root/build/deps/src"
src="$src_root/onnxruntime-$ORT_VERSION"
tarball="$src_root/onnxruntime-$ORT_VERSION.tar.gz"
mkdir -p "$src_root"
if [[ ! -d "$src" ]]; then
    if [[ ! -f "$tarball" ]]; then
        curl -sSL -o "$tarball.part" "https://github.com/microsoft/onnxruntime/archive/refs/tags/v$ORT_VERSION.tar.gz"
        mv "$tarball.part" "$tarball"
    fi
    echo "$ORT_SHA256  $tarball" | shasum -a 256 -c -
    tar -xzf "$tarball" -C "$src_root"
fi

# a private environment, so the build does not depend on what the system Python has installed
venv="$root/build/deps/onnxruntime-venv"
if [[ ! -x "$venv/bin/python" ]]; then
    "$python" -m venv "$venv"
fi
"$venv/bin/python" -m pip install --quiet --disable-pip-version-check "flatbuffers==$FLATBUFFERS_VERSION"

build="$root/build/deps/onnxruntime-build-$arch"
"$venv/bin/python" "$src/tools/ci_build/build.py" \
    --build_dir "$build" \
    --config Release \
    --update --build --parallel \
    --skip_tests \
    --skip_submodule_sync \
    --compile_no_warning_as_error \
    --include_ops_by_config "$config" \
    --disable_ml_ops \
    --osx_arch "$arch" \
    --apple_deploy_target "$deploy_target" \
    --cmake_generator Ninja \
    --cmake_extra_defines CMAKE_POLICY_VERSION_MINIMUM=3.5 onnxruntime_BUILD_UNIT_TESTS=OFF \
    CMAKE_OSX_DEPLOYMENT_TARGET="$deploy_target" CMAKE_OSX_ARCHITECTURES="$arch" \
    onnxruntime_BUILD_SHARED_LIB=OFF CMAKE_C_VISIBILITY_PRESET=hidden CMAKE_CXX_VISIBILITY_PRESET=hidden \
    CMAKE_VISIBILITY_INLINES_HIDDEN=ON

# one archive with every static library the session needs (onnxruntime_*, mlas, abseil, flatbuffers, ...)
rm -rf "$out"
mkdir -p "$out/lib" "$out/include"
libs=()
while IFS= read -r lib; do
    libs+=("$lib")
done < <(find "$build/Release" -name '*.a' ! -name 'libgtest*' ! -name 'libgmock*' ! -name '*test*' | sort)
libtool -static -no_warning_for_no_symbols -o "$out/lib/libonnxruntime.a" "${libs[@]}"
cp "$src/include/onnxruntime/core/session/"*.h "$out/include/"
cp "$src/LICENSE" "$out/LICENSE"
touch "$stamp"
echo "installed onnxruntime $ORT_VERSION ($arch) to $out ($(du -h "$out/lib/libonnxruntime.a" | cut -f1))"
