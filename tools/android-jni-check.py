#!/usr/bin/env python3
"""Checks that the native code and Bridge.java agree on every JNI call.

A mismatch builds fine and crashes on the TV (NoSuchMethodError,
UnsatisfiedLinkError), so CI runs this after the Java compile:

  python3 tools/android-jni-check.py <dir with compiled classes>

- each android::method("name", "signature") in src/platform/android must be
  a static method of org.marigold.app.Bridge with exactly that signature;
- each Java_org_marigold_app_Bridge_* function must match a native method
  of Bridge, parameter for parameter.
"""
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
NATIVE = ROOT / "src/platform/android"
JNI_TYPES = {
    "jint": "I", "jlong": "J", "jboolean": "Z", "jfloat": "F", "jdouble": "D",
    "jbyteArray": "[B", "jdoubleArray": "[D", "jstring": "Ljava/lang/String;",
}


def bridge_methods(classes):
    home = os.environ.get("JAVA_HOME")
    javap = os.path.join(home, "bin", "javap") if home else "javap"
    out = subprocess.run([javap, "-s", "-p", "-cp", classes, "org.marigold.app.Bridge"],
                         capture_output=True, text=True, check=True).stdout
    methods = {}
    lines = out.splitlines()
    for i, line in enumerate(lines):
        m = re.search(r"\b(\w+)\(.*\);$", line.strip())
        if not m or i + 1 >= len(lines) or "descriptor:" not in lines[i + 1]:
            continue
        desc = lines[i + 1].split("descriptor:")[1].strip()
        methods.setdefault(m.group(1), []).append((desc, "static" in line, "native" in line))
    return methods


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    methods = bridge_methods(sys.argv[1])
    errors = []
    sources = {p: p.read_text() for p in NATIVE.glob("*.cpp")}
    for path, text in sources.items():
        for name, sig in re.findall(r'method\("(\w+)",\s*"([^"]+)"\)', text):
            found = [d for d, static, _ in methods.get(name, []) if static and d == sig]
            if not found:
                errors.append(f"{path.name}: Bridge.{name}{sig} is not a static method of Bridge")
        for m in re.finditer(r"Java_org_marigold_app_Bridge_(\w+)\(([^)]*)\)", text):
            name, params = m.group(1), [p.strip() for p in m.group(2).split(",")]
            types = [p.split()[0] for p in params[2:]]  # after JNIEnv* and jclass
            args = ""
            for t in types:
                if t == "jobject":
                    args += "L"  # any object: matched by prefix below
                elif t in JNI_TYPES:
                    args += JNI_TYPES[t]
                else:
                    errors.append(f"{path.name}: {name}: unknown JNI type {t}")
            natives = [d for d, static, native in methods.get(name, []) if native and static]
            if not natives:
                errors.append(f"{path.name}: Bridge has no native {name}")
                continue
            java_args = re.sub(r"L[^;]+;", "L", natives[0][1:natives[0].index(")")])
            if java_args != args:
                errors.append(f"{path.name}: {name} takes ({args}) here but ({java_args}) in Bridge")
    native_decls = {n for n, ds in methods.items() for _, _, native in ds if native}
    exported = {n for t in sources.values() for n in re.findall(r"Java_org_marigold_app_Bridge_(\w+)\(", t)}
    for n in sorted(native_decls - exported):
        errors.append(f"Bridge.{n} is declared native but no Java_org_marigold_app_Bridge_{n} exists")
    for e in errors:
        print("jni-check:", e)
    if errors:
        sys.exit(1)
    print(f"jni-check: ok ({len(native_decls)} natives, "
          f"{sum(len(re.findall(r'method[(]', t)) for t in sources.values())} Java calls)")


if __name__ == "__main__":
    main()
