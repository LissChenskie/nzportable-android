#!/usr/bin/env python3
"""Build an optimized test APK with the SDK tools directly (no Maven/Gradle required)."""
import argparse
import os
import pathlib
import shutil
import subprocess
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

def run(*args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)

def build(abi, sdk, java, jobs):
    ndk = sdk / "ndk/27.2.12479018"
    cmake = sdk / "cmake/3.22.1/bin/cmake"
    native = ROOT / "build" / f"android-{abi}"
    out = ROOT / "build" / f"apk-{abi}"
    out.mkdir(parents=True, exist_ok=True)
    run(cmake, "-S", ROOT / "android", "-B", native, "-G", "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={cmake.parent / 'ninja'}",
        f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build/cmake/android.toolchain.cmake'}",
        f"-DANDROID_ABI={abi}", "-DANDROID_PLATFORM=android-26", "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=ON")
    run(cmake, "--build", native, "-j", jobs)
    tools = sdk / "build-tools/35.0.0"
    android_jar = sdk / "platforms/android-35/android.jar"
    classes = out / "classes"
    classes.mkdir(exist_ok=True)
    sources = sorted((ROOT / "android/app/src/main/java").rglob("*.java"))
    sources += sorted((ROOT / "third_party/SDL/android-project/app/src/main/java").rglob("*.java"))
    # Java argument files avoid command line length limits and keep paths with spaces safe.
    argfile = out / "java-sources.txt"
    argfile.write_text("\n".join('"' + str(s).replace('\\', '\\\\') + '"' for s in sources))
    run(java / "javac", "-encoding", "UTF-8", "--release", "8", "-classpath", android_jar,
        "-d", classes, "@" + str(argfile))
    jar = out / "classes.jar"
    run(java / "jar", "cf", jar, "-C", classes, ".")
    dex = out / "dex"
    dex.mkdir(exist_ok=True)
    run(java / "java", "-cp", tools / "lib/d8.jar", "com.android.tools.r8.D8", "--release", "--min-api", "26", "--lib", android_jar, "--output", dex, jar)
    # Fully qualify manifest classes: aapt2's --rename-manifest-package does not
    # provide AGP's namespace injection for a manifest without a package attribute.
    manifest = (ROOT / "android/app/src/main/AndroidManifest.xml").read_text()
    manifest = manifest.replace('<manifest xmlns:', '<manifest package="org.nzportable.android" xmlns:')
    manifest = manifest.replace('android:extractNativeLibs="false"', 'android:extractNativeLibs="false" android:debuggable="true"')
    temp_manifest = out / "AndroidManifest.xml"
    temp_manifest.write_text(manifest)
    unsigned = out / "unsigned.apk"
    run(tools / "aapt2", "link", "-I", android_jar, "--manifest", temp_manifest, "--min-sdk-version", "26",
        "--target-sdk-version", "35", "--version-code", "1", "--version-name", "0.1.0", "-o", unsigned)
    libraries = {"libmain.so": native / "libmain.so", "libSDL2.so": native / "SDL/libSDL2.so", "libSDL2_mixer.so": native / "SDL_mixer/libSDL2_mixer.so"}
    host_tag = "linux-x86_64" if os.name != "nt" else "windows-x86_64"
    strip = ndk / "toolchains/llvm/prebuilt" / host_tag / "bin/llvm-strip"
    with zipfile.ZipFile(unsigned, "a") as apk:
        for name, lib in libraries.items():
            stripped = out / name
            shutil.copy2(lib, stripped)
            run(strip, "--strip-unneeded", stripped)
            apk.write(stripped, f"lib/{abi}/{name}", compress_type=zipfile.ZIP_STORED)
        for path in dex.glob("*.dex"):
            apk.write(path, path.name, compress_type=zipfile.ZIP_DEFLATED)
        assets = ROOT / "android/app/src/main/assets"
        if assets.exists():
            for path in assets.rglob("*"):
                if path.is_file():
                    apk.write(path, "assets/" + path.relative_to(assets).as_posix(), compress_type=zipfile.ZIP_STORED)
    aligned = out / "aligned.apk"
    run(tools / "zipalign", "-f", "-P", "16", "4", unsigned, aligned)
    key = ROOT / "build/android-test.keystore"
    if not key.exists():
        run(java / "keytool", "-genkeypair", "-keystore", key, "-storepass", "android", "-keypass", "android",
            "-alias", "androiddebugkey", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
            "-dname", "CN=NZPortable Android Test,O=Development,C=US")
    final = ROOT / "build" / f"nzportable-android-{abi}.apk"
    run(java / "java", "-jar", tools / "lib/apksigner.jar", "sign", "--ks", key, "--ks-pass", "pass:android",
        "--key-pass", "pass:android", "--out", final, aligned)
    run(java / "java", "-jar", tools / "lib/apksigner.jar", "verify", "--verbose", final)
    run(tools / "zipalign", "-c", "-P", "16", "4", final)
    print(f"APK: {final}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--abi", choices=["arm64-v8a", "armeabi-v7a", "x86_64"], default="arm64-v8a")
    parser.add_argument("--sdk", type=pathlib.Path, default=os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT"))
    parser.add_argument("--java-home", type=pathlib.Path, default=os.environ.get("JAVA_HOME"))
    parser.add_argument("--jobs", default="4")
    args = parser.parse_args()
    if not args.sdk or not args.java_home:
        parser.error("Set ANDROID_HOME and JAVA_HOME, or pass --sdk and --java-home")
    build(args.abi, args.sdk.resolve(), args.java_home.resolve() / "bin", args.jobs)
