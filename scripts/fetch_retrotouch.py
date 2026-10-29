#!/usr/bin/env python3
"""Fetch a reviewed, pinned MIT RetroTouch AAR and generate a Java bridge import."""
import hashlib
import pathlib
import shutil
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
REV = "bbb088abeb22444da362e6c294bfb60ec4c7b1e6"
BLOB = "a16bec761942ae64bd38f49259dcfa73189d6e78"
URL = f"https://raw.githubusercontent.com/Andiweli/RetroTouch/{REV}/source/RetroTouch-1.0.0-beta.7.aar"
AAR = ROOT / "android/app/libs/retrotouch.aar"
OUT = ROOT / "android/app/build/generated/nzp"
JAR = ROOT / "build/third_party/retrotouch-classes.jar"
TEMPLATE = ROOT / "android/RetroTouchBridge.java.in"

def fetch():
    AAR.parent.mkdir(parents=True, exist_ok=True)
    if AAR.is_file():
        data = AAR.read_bytes()
    else:
        with urllib.request.urlopen(URL, timeout=90) as request:
            data = request.read()
    header = b"blob " + str(len(data)).encode("ascii") + b"\0"
    actual = hashlib.sha1(header + data).hexdigest()
    if actual != BLOB:
        raise RuntimeError(f"RetroTouch changed or corrupted (Git blob {actual}, expected {BLOB})")
    if not AAR.is_file():
        AAR.write_bytes(data)
    JAR.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(AAR) as aar:
        content = aar.read("classes.jar")
    JAR.write_bytes(content)
    with zipfile.ZipFile(JAR) as jar:
        classes = jar.namelist()
    wanted = ("RetroTouchView", "RetroTouchControl", "RetroTouchLayout",
              "RetroTouchAdapter", "RetroTouchMode", "RetroTouchControllers")
    imports = []
    for item in wanted:
        matches = [x for x in classes if x.endswith("/" + item + ".class") and "$" not in x]
        if len(matches) != 1:
            raise RuntimeError(f"RetroTouch class {item} not uniquely found: {matches}")
        imports.append("import " + matches[0][:-6].replace("/", ".") + ";")
    src = TEMPLATE.read_text(encoding="utf-8").replace("// __RETROTOUCH_IMPORTS__", "\n".join(imports))
    generated = OUT / "org/nzportable/android/RetroTouchBridge.java"
    generated.parent.mkdir(parents=True, exist_ok=True)
    generated.write_text(src, encoding="utf-8")
    print("RetroTouch verified:", BLOB, "|", len(content), "jar bytes", "|", len(classes), "classes")
    print("Bridge imports:", ", ".join(imports))
    return JAR, generated

if __name__ == "__main__":
    fetch()
