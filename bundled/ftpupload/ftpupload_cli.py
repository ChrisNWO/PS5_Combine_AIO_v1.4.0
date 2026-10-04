#!/usr/bin/env python3
"""ftpupload-cli — загрузка готового файла на консоль по FTP (часть PS5 Combine AIO).

Написан с нуля на стандартной библиотеке Python (ftplib, без сторонних пакетов) под простые анонимные
FTP-серверы, которые дают домашние приложения на PS5 (например, встроенный FTP у etaHEN, порт по умолчанию
1337; значение порта не захардкожено — задаётся пользователем, у разных решений он может быть другим).
Создаёт отсутствующие папки в указанном пути сам (большинство таких серверов этого не делают).

  ftpupload-cli --host 192.168.1.50 --port 1337 --remote-dir /data/homebrew --file "C:\\path\\game.exfat"
                [--user anonymous] [--password ""] [--passive] [--timeout 20] [--name другое-имя.exfat]

Строки прогресса вида "[####------] 45.0% · имя" — их разбирает ToolRunner в GUI.
"""
import argparse
import ftplib
import os
import sys
import time


def bar(fraction, text):
    fraction = max(0.0, min(1.0, fraction))
    filled = int(round(fraction * 24))
    print("[%s%s] %5.1f%% · %s" % ("#" * filled, "-" * (24 - filled), fraction * 100, text), flush=True)


def ensure_remote_dir(ftp: ftplib.FTP, remote_dir: str) -> None:
    """Создаёт /a/b/c по частям; если часть уже существует — просто переходит в неё."""
    remote_dir = remote_dir.replace("\\", "/")
    parts = [p for p in remote_dir.split("/") if p]
    if remote_dir.startswith("/"):
        ftp.cwd("/")
    for part in parts:
        try:
            ftp.cwd(part)
        except ftplib.error_perm:
            ftp.mkd(part)
            ftp.cwd(part)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Upload one file to a PS5 over plain FTP")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=21)
    ap.add_argument("--user", default="anonymous")
    ap.add_argument("--password", default="")
    ap.add_argument("--remote-dir", required=True, help="e.g. /data/homebrew")
    ap.add_argument("--file", required=True, help="local file to upload")
    ap.add_argument("--name", default=None, help="remote file name (default: same as local)")
    ap.add_argument("--timeout", type=int, default=20)
    ap.add_argument("--passive", action="store_true", default=True)
    ap.add_argument("--active", dest="passive", action="store_false", help="use active mode instead of passive")
    args = ap.parse_args(argv)

    if not os.path.isfile(args.file):
        print("ERROR: local file not found: %s" % args.file)
        return 2
    total = os.path.getsize(args.file)
    remote_name = args.name or os.path.basename(args.file)

    print("Connecting to %s:%d ..." % (args.host, args.port))
    ftp = ftplib.FTP()
    ftp.set_pasv(args.passive)
    try:
        ftp.connect(args.host, args.port, timeout=args.timeout)
        ftp.login(args.user, args.password)
    except (OSError, ftplib.all_errors) as err:
        print("ERROR: cannot connect/login: %s" % err)
        return 1

    try:
        ensure_remote_dir(ftp, args.remote_dir)
    except ftplib.all_errors as err:
        print("ERROR: cannot reach/create remote folder %s: %s" % (args.remote_dir, err))
        try:
            ftp.quit()
        except ftplib.all_errors:
            pass
        return 1

    done = 0
    last = 0.0

    def on_block(block: bytes) -> None:
        nonlocal done, last
        done += len(block)
        now = time.monotonic()
        if now - last >= 0.2 or done >= total:
            bar(done / total if total else 1.0, remote_name)
            last = now

    try:
        with open(args.file, "rb") as fh:
            ftp.storbinary("STOR " + remote_name, fh, blocksize=1 << 16, callback=on_block)
    except ftplib.all_errors as err:
        print("ERROR: upload failed: %s" % err)
        try:
            ftp.quit()
        except ftplib.all_errors:
            pass
        return 1

    bar(1.0, remote_name)
    try:
        ftp.quit()
    except ftplib.all_errors:
        pass
    print("Uploaded %s (%d bytes) to %s:%d%s/%s" % (args.file, total, args.host, args.port, args.remote_dir, remote_name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
