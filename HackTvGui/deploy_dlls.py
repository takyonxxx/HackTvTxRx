#!/usr/bin/env python3
"""
deploy_dlls.py - Recursively copy all MSYS2/mingw64 DLL dependencies.
Called by qmake post-link. Works from cmd.exe (no bash needed).

Usage: python deploy_dlls.py <exe_path> <dest_dir>
"""
import subprocess, sys, os, shutil, re

def get_mingw_deps(binary, msys2_ldd=r"C:\msys64\usr\bin\ldd.exe"):
    """Run ldd and return list of mingw64 DLL paths."""
    try:
        r = subprocess.run([msys2_ldd, binary], capture_output=True, text=True, timeout=30)
        dlls = []
        for line in r.stdout.splitlines():
            # Match lines like: libfoo.dll => /mingw64/bin/libfoo.dll (0x...)
            m = re.search(r'=>\s+(/mingw64/\S+)', line)
            if m:
                # Convert MSYS2 path to Windows path
                msys_path = m.group(1)
                win_path = r"C:\msys64" + msys_path.replace('/', '\\')
                if os.path.exists(win_path):
                    dlls.append(win_path)
        return dlls
    except Exception as e:
        print(f"  ldd failed for {binary}: {e}")
        return []

def deploy(exe_path, dest_dir):
    copied = set()
    to_scan = [exe_path]

    # Also scan all DLLs already in dest_dir (e.g. HackTvLib.dll, avcodec etc.)
    if os.path.isdir(dest_dir):
        for f in os.listdir(dest_dir):
            if f.lower().endswith('.dll'):
                to_scan.append(os.path.join(dest_dir, f))

    while to_scan:
        binary = to_scan.pop(0)
        deps = get_mingw_deps(binary)
        for dll_path in deps:
            basename = os.path.basename(dll_path)
            dest_path = os.path.join(dest_dir, basename)
            if basename.lower() not in copied and not os.path.exists(dest_path):
                try:
                    shutil.copy2(dll_path, dest_path)
                    print(f"  Copied: {basename}")
                    copied.add(basename.lower())
                    # Recurse: scan this new DLL for its own deps
                    to_scan.append(dest_path)
                except Exception as e:
                    print(f"  Failed: {basename}: {e}")
            else:
                copied.add(basename.lower())

    print(f"  Total MSYS2 DLLs deployed: {len(copied)}")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python deploy_dlls.py <exe_path> <dest_dir>")
        sys.exit(1)
    exe = sys.argv[1]
    dst = sys.argv[2]
    print(f"=== deploy_dlls.py ===")
    deploy(exe, dst)
    print(f"=== Done ===")
