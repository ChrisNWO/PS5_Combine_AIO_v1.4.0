#!/usr/bin/env python3
"""fself-cli — рекурсивная fake-подпись исполняемых файлов дампа игры PS5 (без смены версии SDK).

Часть PS5 Combine AIO. Повторяет логику ps5-make-fself-recursive (Alex Free, BSD-3-Clause): в папке дампа находит файлы
.bin / .elf / .prx / .sprx и превращает каждый ELF в fake-signed SELF с помощью make_fself.py (ps5-payload-dev/sdk, John Törnblom).
Отличия: запись во временный файл с атомарной заменой (сбой не портит исходный файл), режим «в копию» (--output),
строки прогресса "[####------] 45%" для GUI, код возврата 1, если хотя бы один ELF подписать не удалось.

  fself-cli --input DUMP [--output COPY] [--paid 0x3100000000000002] [--ptype fake] [--overwrite] [--dry-run]
"""
import argparse
import os
import shutil
import sys
import time

import make_fself as mf

EXTENSIONS = (".bin", ".elf", ".prx", ".sprx")
ELF_MAGIC = b"\x7fELF"


def bar(fraction, text):
    fraction = max(0.0, min(1.0, fraction))
    filled = int(round(fraction * 24))
    print("[%s%s] %5.1f%% · %s" % ("#" * filled, "-" * (24 - filled), fraction * 100, text), flush=True)


def is_elf(path):
    try:
        with open(path, "rb") as f:
            return f.read(4) == ELF_MAGIC
    except OSError:
        return False


def ptype_value(name):
    table = {
        "fake": mf.SignedElfExInfo.PTYPE_FAKE,
        "npdrm_exec": mf.SignedElfExInfo.PTYPE_NPDRM_EXEC,
        "npdrm_dynlib": mf.SignedElfExInfo.PTYPE_NPDRM_DYNLIB,
        "system_exec": mf.SignedElfExInfo.PTYPE_SYSTEM_EXEC,
        "system_dynlib": mf.SignedElfExInfo.PTYPE_SYSTEM_DYNLIB,
    }
    key = (name or "fake").strip().lower()
    if key in table:
        return table[key]
    value = mf.try_parse_int(key)
    if value is None:
        raise ValueError("invalid program type: %s" % name)
    return value


def copy_tree(src, dst):
    """Копия папки игры с прогрессом по байтам."""
    files = []
    total = 0
    for root, _dirs, names in os.walk(src):
        for n in names:
            p = os.path.join(root, n)
            files.append(p)
            total += os.path.getsize(p)
    done = 0
    last = 0.0
    for p in files:
        rel = os.path.relpath(p, src)
        target = os.path.join(dst, rel)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(p, target)
        done += os.path.getsize(p)
        now = time.monotonic()
        if now - last >= 0.25:
            bar(done / total if total else 1.0, "copy " + rel)
            last = now
    # пустые папки тоже переносим
    for root, dirs, _names in os.walk(src):
        for d in dirs:
            os.makedirs(os.path.join(dst, os.path.relpath(os.path.join(root, d), src)), exist_ok=True)
    bar(1.0, "copied %d file(s)" % len(files))


def sign_file(path, paid, ptype):
    """ELF -> fake-signed SELF на месте; запись через временный файл."""
    with open(path, "rb") as f:
        elf = mf.ElfFile(ignore_shdrs=True)
        elf.load(f)
    tmp = path + ".fself.tmp"
    try:
        with open(tmp, "wb") as f:
            mf.SignedElfFile(elf, paid=paid, ptype=ptype, app_version=0, fw_version=0, auth_info=None).save(f)
        os.replace(tmp, path)
    except Exception:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise


def main(argv=None):
    ap = argparse.ArgumentParser(description="Recursive fake-signing of a decrypted PS5 game dump")
    ap.add_argument("--input", "-i", required=True, help="game dump folder (eboot.bin in its root)")
    ap.add_argument("--output", "-o", help="copy the dump here first and sign the copy (default: sign in place)")
    ap.add_argument("--paid", default="0x3100000000000002", help="program authentication id")
    ap.add_argument("--ptype", default="fake", help="program type (default: fake)")
    ap.add_argument("--overwrite", action="store_true", help="allow a non-empty output folder")
    ap.add_argument("--dry-run", action="store_true", help="only list what would be signed")
    args = ap.parse_args(argv)

    src = os.path.abspath(args.input)
    if not os.path.isfile(os.path.join(src, "eboot.bin")):
        print("ERROR: %s is not a game folder (eboot.bin not found in its root)" % src)
        return 2
    try:
        paid = int(args.paid, 0)
        ptype = ptype_value(args.ptype)
    except ValueError as err:
        print("ERROR: %s" % err)
        return 2

    root = src
    if args.output and not args.dry_run:
        dst = os.path.abspath(args.output)
        if dst == src or dst.startswith(src + os.sep):
            print("ERROR: the output folder must not be the dump folder or lie inside it")
            return 2
        if os.path.isdir(dst) and os.listdir(dst) and not args.overwrite:
            print("ERROR: the output folder is not empty (use --overwrite to allow it)")
            return 2
        os.makedirs(dst, exist_ok=True)
        print("Copying the dump to %s ..." % dst)
        copy_tree(src, dst)
        root = dst

    candidates = []
    for dirpath, _dirs, names in os.walk(root):
        for n in names:
            if n.lower().endswith(EXTENSIONS):
                candidates.append(os.path.join(dirpath, n))
    candidates.sort()
    print("Found %d candidate file(s) (*.bin, *.elf, *.prx, *.sprx)" % len(candidates))

    signed = skipped = failed = 0
    for i, path in enumerate(candidates, 1):
        rel = os.path.relpath(path, root).replace("\\", "/")
        bar((i - 1) / max(1, len(candidates)), rel)
        if not is_elf(path):
            skipped += 1                      # уже подписан, либо это просто данные с расширением .bin
            continue
        if args.dry_run:
            print("would sign: " + rel)
            signed += 1
            continue
        try:
            sign_file(path, paid, ptype)
            signed += 1
            print("signed: " + rel)
        except Exception as err:               # один плохой файл не должен останавливать остальные
            failed += 1
            print("FAILED: %s (%s)" % (rel, err))
    bar(1.0, "done")
    print("Fake signed %d file(s); skipped %d (not a plain ELF: already signed or data); failed %d." % (signed, skipped, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
