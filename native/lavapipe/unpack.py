# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Unpacks part of a .tar.xz, for build.bat: Windows' tar.exe hung on the LLVM sources on
# the CI runners, without a word. This says what it does as it goes.
#
#   python unpack.py <archive> <folder> <path> [<path> ...] [--exclude <path> ...]
#
# Unpacks the entries under each <path> into <folder>, except those under a path given
# after --exclude (every path after it is excluded). Symbolic links are left out (making
# them needs rights on Windows).
import sys
import tarfile
import time


def under(name, paths):
    return any(name == p or name.startswith(p.rstrip("/") + "/") for p in paths)


def main(argv):
    if len(argv) < 3:
        print("usage: unpack.py <archive> <folder> <path> ... [--exclude <path> ...]")
        return 2
    archive, folder, rest = argv[0], argv[1], argv[2:]
    include, exclude = [], []
    target = include
    for arg in rest:
        if arg == "--exclude":
            target = exclude
        else:
            target.append(arg)
    start = time.monotonic()
    files = skipped = 0
    # Stream mode: one pass through the archive, no seeking back.
    with tarfile.open(archive, "r|xz") as tar:
        for member in tar:
            if not under(member.name, include) or under(member.name, exclude):
                continue
            if member.issym() or member.islnk():
                skipped += 1
                continue
            if hasattr(tarfile, "data_filter"):
                tar.extract(member, folder, filter="data")
            else:
                tar.extract(member, folder)
            if member.isfile():
                files += 1
                if files % 5000 == 0:
                    print(f"  {files} files ({time.monotonic() - start:.0f} s)", flush=True)
    print(f"  unpacked {files} files, left out {skipped} links ({time.monotonic() - start:.0f} s)", flush=True)
    return 0 if files > 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
