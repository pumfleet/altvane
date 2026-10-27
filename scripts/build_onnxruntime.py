#!/usr/bin/env python3
"""Build a small, static, CPU-only ONNX Runtime for the products that run neural networks.

    scripts/build_onnxruntime.py <arch> <operators.config> <out-dir> [--config Release|Debug]
                                 [--macos-target 11.0] [--cmake <path>]

Only the operators listed in the config (see products/dialogue/models/README.md) are compiled in, and everything is
combined into one static library, lib/onnxruntime.lib on Windows and lib/libonnxruntime.a elsewhere, so the plug-in
carries no dynamic library. The C/C++ API headers go to include/. Nothing is built when out-dir already holds a build
with the same version, operators and options.

<arch> is the CPU to build for: arm64 or x86_64 on macOS (from either), elsewhere the host's. On Windows, run it from
a Visual Studio developer environment (vcvars) for that CPU; ONNX Runtime is built with the static MSVC runtime, like
the plug-ins (cmake/JUCEDefaults.cmake).

Needs Python >= 3.10, CMake, Ninja and a C++20 compiler.
"""
import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import venv
from pathlib import Path

ORT_VERSION = "1.28.0"
ORT_SHA256 = "9616cbdbbfcb1420b3261cd280a047d74ab0a249825e577b0e2dd310e22f6b83"
FLATBUFFERS_VERSION = "25.12.19"  # Python package the operator reduction needs

ROOT = Path(__file__).resolve().parent.parent
DEPS = ROOT / "build" / "deps"
SYSTEM = platform.system()


def run(*command, **kwargs):
    print("+", " ".join(str(c) for c in command), flush=True)
    subprocess.run([str(c) for c in command], check=True, **kwargs)


def fetch_source():
    src = DEPS / "src" / f"onnxruntime-{ORT_VERSION}"
    if src.is_dir():
        return src
    tarball = src.with_name(f"{src.name}.tar.gz")
    tarball.parent.mkdir(parents=True, exist_ok=True)
    if not tarball.exists():
        part = tarball.with_name(f"{tarball.name}.part")
        url = f"https://github.com/microsoft/onnxruntime/archive/refs/tags/v{ORT_VERSION}.tar.gz"
        print(f"Downloading {url}", flush=True)
        urllib.request.urlretrieve(url, part)
        part.replace(tarball)
    digest = hashlib.sha256(tarball.read_bytes()).hexdigest()
    if digest != ORT_SHA256:
        tarball.unlink()
        sys.exit(f"{tarball}: SHA-256 is {digest}, expected {ORT_SHA256}")
    # extract next to it and move into place, so an interrupted extraction is never mistaken for the source
    staging = src.with_name(f"{src.name}.part")
    shutil.rmtree(staging, ignore_errors=True)
    with tarfile.open(tarball) as archive:
        archive.extractall(staging, **({"filter": "data"} if hasattr(tarfile, "data_filter") else {}))
    (staging / src.name).replace(src)
    shutil.rmtree(staging)
    return src


def python_env():
    # a private environment, so the build does not depend on what the system Python has installed
    env_dir = DEPS / "onnxruntime-venv"
    python = env_dir / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not python.exists():
        venv.create(env_dir, with_pip=True)
    run(python, "-m", "pip", "install", "--quiet", "--disable-pip-version-check",
        f"flatbuffers=={FLATBUFFERS_VERSION}")
    return python


def combine(libs, out_lib):
    """one archive with every static library the session needs (onnxruntime_*, mlas, abseil, protobuf, ...)"""
    if SYSTEM == "Darwin":
        run("libtool", "-static", "-no_warning_for_no_symbols", "-o", out_lib, *libs)
    elif SYSTEM == "Windows":
        response = out_lib.with_suffix(".rsp")
        response.write_text("".join(f'"{lib}"\n' for lib in libs))
        run("lib", "/NOLOGO", f"/OUT:{out_lib}", f"@{response}")
        response.unlink()
    else:
        script = f"CREATE {out_lib}\n" + "".join(f"ADDLIB {lib}\n" for lib in libs) + "SAVE\nEND\n"
        run("ar", "-M", input=script, text=True)


def main():
    if sys.version_info < (3, 10):
        sys.exit("building ONNX Runtime needs Python 3.10 or newer")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("arch")
    parser.add_argument("operators", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--config", default="Release", choices=["Release", "Debug"])
    parser.add_argument("--macos-target", default="", help="minimum macOS version (default 11.0)")
    parser.add_argument("--cmake", default="cmake", help="the CMake to build with")
    args = parser.parse_args()

    operators = args.operators.resolve()
    out = args.out.resolve()
    macos_target = args.macos_target or "11.0"
    if "." not in macos_target:
        macos_target += ".0"

    # everything that changes the result; the script itself too, as it decides how ONNX Runtime is configured
    key = hashlib.sha256()
    for part in (ORT_VERSION, args.arch, args.config, macos_target if SYSTEM == "Darwin" else ""):
        key.update(part.encode() + b"\0")
    key.update(operators.read_bytes())
    key.update(Path(__file__).read_bytes())
    stamp = out / ".stamp"
    out_lib = out / "lib" / ("onnxruntime.lib" if SYSTEM == "Windows" else "libonnxruntime.a")
    if out_lib.exists() and stamp.exists() and stamp.read_text() == key.hexdigest():
        print(f"onnxruntime {ORT_VERSION} ({args.arch}, {args.config}) is up to date in {out}")
        return

    src = fetch_source()
    python = python_env()
    build = DEPS / f"onnxruntime-build-{args.arch}"
    command = [python, src / "tools" / "ci_build" / "build.py",
               "--build_dir", build,
               "--config", args.config,
               "--update", "--build", "--parallel",
               "--skip_tests",
               "--skip_submodule_sync",
               "--compile_no_warning_as_error",
               "--include_ops_by_config", operators,
               "--disable_ml_ops",
               "--cmake_path", args.cmake,
               "--cmake_generator", "Ninja"]
    defines = ["CMAKE_POLICY_VERSION_MINIMUM=3.5", "onnxruntime_BUILD_UNIT_TESTS=OFF",
               "onnxruntime_BUILD_SHARED_LIB=OFF", "CMAKE_C_VISIBILITY_PRESET=hidden",
               "CMAKE_CXX_VISIBILITY_PRESET=hidden", "CMAKE_VISIBILITY_INLINES_HIDDEN=ON"]
    if SYSTEM == "Darwin":
        command += ["--osx_arch", args.arch, "--apple_deploy_target", macos_target]
        defines += [f"CMAKE_OSX_DEPLOYMENT_TARGET={macos_target}", f"CMAKE_OSX_ARCHITECTURES={args.arch}"]
    elif SYSTEM == "Windows":
        command += ["--enable_msvc_static_runtime"]
    run(*command, "--cmake_extra_defines", *defines)

    built = build / args.config
    if SYSTEM == "Windows":
        # import libraries of the DLLs ONNX Runtime also builds (onnxruntime_providers_shared) stay out
        libs = [lib for lib in built.rglob("*.lib") if not lib.with_suffix(".dll").exists()]
    else:
        libs = list(built.rglob("*.a"))
    libs = sorted(lib for lib in libs if "test" not in lib.name and "gmock" not in lib.name)

    shutil.rmtree(out, ignore_errors=True)
    (out / "lib").mkdir(parents=True)
    (out / "include").mkdir()
    combine(libs, out_lib)
    for header in (src / "include" / "onnxruntime" / "core" / "session").glob("*.h"):
        shutil.copy2(header, out / "include")
    shutil.copy2(src / "LICENSE", out / "LICENSE")
    stamp.write_text(key.hexdigest())
    size = out_lib.stat().st_size / 2 ** 20
    print(f"installed onnxruntime {ORT_VERSION} ({args.arch}, {args.config}) to {out} ({size:.0f} MB)")


if __name__ == "__main__":
    main()
