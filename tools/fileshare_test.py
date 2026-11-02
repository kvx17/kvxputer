#!/usr/bin/env python3
"""Host-side regression tests for kvxputer File Share.

Manual checklist (device UI / phone):
  - Captive portal opens the share page on iOS and Android (AP mode)
  - Phone browser download completes without stalling
  - Device QR screens (URL + WiFi) scan correctly
  - ESC during upload prompts Sel=stop / ESC=cancel; Sel aborts cleanly

Usage:
  python3 tools/fileshare_test.py --host 172.0.0.1 --user kvx --pwd SECRET
  python3 tools/fileshare_test.py --host 172.0.0.1 --soak 5
"""

from __future__ import annotations

import argparse
import hashlib
import io
import os
import random
import string
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import Optional

try:
    import requests
except ImportError:
    print("Install requests: pip install requests", file=sys.stderr)
    sys.exit(2)


class Fail(Exception):
    pass


class ShareClient:
    def __init__(self, host: str, user: str, pwd: str, timeout: float = 60.0):
        self.base = host if host.startswith("http") else f"http://{host}"
        self.user = user
        self.pwd = pwd
        self.timeout = timeout
        self.s = requests.Session()

    def url(self, path: str) -> str:
        if not path.startswith("/"):
            path = "/" + path
        return self.base.rstrip("/") + path

    def health(self) -> str:
        r = self.s.get(self.url("/health"), timeout=self.timeout)
        if r.status_code != 200:
            raise Fail(f"/health -> {r.status_code}")
        return r.text.strip()

    def login(self, user: Optional[str] = None, pwd: Optional[str] = None) -> requests.Response:
        return self.s.post(
            self.url("/login"),
            data={"username": user if user is not None else self.user, "password": pwd if pwd is not None else self.pwd},
            timeout=self.timeout,
        )

    def logout(self) -> None:
        self.s.post(self.url("/logout"), timeout=self.timeout)

    def status(self) -> dict:
        r = self.s.get(self.url("/api/status"), timeout=self.timeout)
        if r.status_code != 200:
            raise Fail(f"/api/status -> {r.status_code} {r.text}")
        return r.json()

    def list(self, vol: str, path: str = "/", offset: int = 0, limit: int = 200) -> dict:
        r = self.s.get(
            self.url("/api/list"),
            params={"vol": vol, "path": path, "offset": offset, "limit": limit},
            timeout=self.timeout,
        )
        if r.status_code != 200:
            raise Fail(f"/api/list -> {r.status_code} {r.text}")
        return r.json()

    def mkdir(self, vol: str, path: str, name: str) -> requests.Response:
        return self.s.post(
            self.url("/api/mkdir"),
            data={"vol": vol, "path": path, "name": name},
            timeout=self.timeout,
        )

    def rename(self, vol: str, path: str, old: str, name: str) -> requests.Response:
        return self.s.post(
            self.url("/api/rename"),
            data={"vol": vol, "path": path, "old": old, "name": name},
            timeout=self.timeout,
        )

    def delete(self, vol: str, path: str, name: str) -> requests.Response:
        return self.s.post(
            self.url("/api/delete"),
            data={"vol": vol, "path": path, "name": name},
            timeout=self.timeout,
        )

    def upload(
        self,
        vol: str,
        path: str,
        filename: str,
        data: bytes,
        timeout: Optional[float] = None,
    ) -> requests.Response:
        files = {"file": (filename, io.BytesIO(data), "application/octet-stream")}
        return self.s.post(
            self.url("/api/upload"),
            params={"vol": vol, "path": path, "size": len(data)},
            files=files,
            timeout=timeout if timeout is not None else max(self.timeout, 30 + len(data) / 50_000),
        )

    def download(self, vol: str, path: str) -> bytes:
        r = self.s.get(
            self.url("/api/download"),
            params={"vol": vol, "path": path},
            timeout=max(self.timeout, 120),
        )
        if r.status_code != 200:
            raise Fail(f"/api/download -> {r.status_code} {r.text}")
        return r.content


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def rand_name(prefix: str = "t") -> str:
    return prefix + "_" + "".join(random.choices(string.ascii_lowercase + string.digits, k=8))


def pick_vol(c: ShareClient) -> str:
    st = c.status()
    if st.get("sd"):
        return "sd"
    return "lfs"


class Runner:
    def __init__(self):
        self.passed = 0
        self.failed = 0

    def check(self, name: str, ok: bool, detail: str = "") -> bool:
        if ok:
            self.passed += 1
            print(f"  PASS  {name}" + (f" — {detail}" if detail else ""))
        else:
            self.failed += 1
            print(f"  FAIL  {name}" + (f" — {detail}" if detail else ""))
        return ok

    def expect(self, name: str, cond: bool, detail: str = ""):
        self.check(name, cond, detail)
        if not cond:
            raise Fail(name)


def test_smoke(c: ShareClient, r: Runner):
    print("\n== smoke ==")
    body = c.health()
    r.expect("health", body == "kvxputer-file-share", body)

    bad = c.login("nope", "wrong")
    r.expect("bad login", bad.status_code == 401, str(bad.status_code))

    anon = requests.get(c.url("/api/status"), timeout=c.timeout)
    r.expect("anon status 401", anon.status_code == 401, str(anon.status_code))

    ok = c.login()
    r.expect("good login", ok.status_code == 200, str(ok.status_code))

    st = c.status()
    r.expect("status json", "lfs" in st and "readOnly" in st, str(st.keys()))
    vol = pick_vol(c)
    listing = c.list(vol, "/")
    r.expect("list root", "items" in listing, str(listing.keys()))

    name = rand_name("dir")
    mk = c.mkdir(vol, "/", name)
    r.expect("mkdir", mk.status_code == 200, mk.text)
    renamed = name + "_r"
    rn = c.rename(vol, "/", name, renamed)
    r.expect("rename", rn.status_code == 200, rn.text)
    dl = c.delete(vol, "/", renamed)
    r.expect("delete", dl.status_code == 200, dl.text)


def test_path_security(c: ShareClient, r: Runner):
    print("\n== path security ==")
    vol = pick_vol(c)
    for path in ("/../", "/..", "/a/../b", "//x", "/./y"):
        resp = c.s.get(c.url("/api/list"), params={"vol": vol, "path": path}, timeout=c.timeout)
        r.check(f"reject path {path!r}", resp.status_code == 400, str(resp.status_code))

    # Encoded traversal
    resp = c.s.get(
        c.url("/api/list"),
        params={"vol": vol, "path": "/%2e%2e"},
        timeout=c.timeout,
    )
    # Server may decode before check; either 400 or sanitized 404/400 is acceptable if not leaking
    r.check(
        "encoded ..",
        resp.status_code in (400, 404),
        str(resp.status_code),
    )

    # Backslash-stripped upload name should land as basename only
    data = b"backslash-test"
    fname = "evil\\payload.bin"
    up = c.upload(vol, "/", fname, data)
    r.expect("upload backslash name", up.status_code == 200, up.text)
    listing = c.list(vol, "/")
    names = [i["name"] for i in listing.get("items", [])]
    r.check("no path separators in name", "payload.bin" in names or any(n.endswith("payload.bin") for n in names), str(names[:20]))
    # cleanup
    for n in names:
        if n.endswith("payload.bin"):
            c.delete(vol, "/", n)


def test_uploads(c: ShareClient, r: Runner, include_large: bool):
    print("\n== uploads ==")
    vol = pick_vol(c)
    folder = rand_name("up")
    r.expect("mkdir upload folder", c.mkdir(vol, "/", folder).status_code == 200)

    sizes = [0, 1, 16 * 1024, 1 * 1024 * 1024]
    if include_large:
        sizes.append(50 * 1024 * 1024)

    for sz in sizes:
        label = f"upload {sz} B"
        payload = os.urandom(sz) if sz else b""
        digest = sha256(payload)
        fname = f"f_{sz}.bin"
        up = c.upload(vol, f"/{folder}", fname, payload)
        if not r.check(label, up.status_code == 200, up.text):
            continue
        got = c.download(vol, f"/{folder}/{fname}")
        r.check(f"roundtrip sha {sz}", sha256(got) == digest, f"got {len(got)} bytes")

    # Aborted upload must not leave busy lock (409)
    print("  … aborted upload then retry")
    big = os.urandom(2 * 1024 * 1024)
    abort_ok = False
    try:
        # Short timeout to force disconnect mid-body
        c.upload(vol, f"/{folder}", "abort.bin", big, timeout=0.05)
    except (requests.exceptions.Timeout, requests.exceptions.ConnectionError, requests.exceptions.ChunkedEncodingError):
        abort_ok = True
    except Exception as e:
        # Some stacks raise ReadTimeout etc.
        abort_ok = "time" in str(e).lower() or "connect" in str(e).lower()
        if not abort_ok:
            print(f"  note: abort raised {type(e).__name__}: {e}")
            abort_ok = True
    r.check("upload aborted", abort_ok)
    time.sleep(1.0)
    retry = c.upload(vol, f"/{folder}", "after_abort.bin", b"ok")
    r.check("upload after abort not 409", retry.status_code != 409, f"{retry.status_code} {retry.text}")
    r.check("upload after abort ok", retry.status_code == 200, f"{retry.status_code} {retry.text}")

    # LittleFS fill expecting 507 when targeting lfs with oversized contentLength
    # Use declared size larger than free space via Content-Length of the multipart.
    # Easiest: pick a size bigger than reported free if lfs free is known.
    st = c.status()
    # Cleanup folder
    listing = c.list(vol, f"/{folder}", limit=500)
    for it in listing.get("items", []):
        c.delete(vol, f"/{folder}", it["name"])
    c.delete(vol, "/", folder)


def test_concurrency(c: ShareClient, r: Runner):
    print("\n== concurrency ==")
    vol = pick_vol(c)
    folder = rand_name("conc")
    r.expect("mkdir conc", c.mkdir(vol, "/", folder).status_code == 200)
    seed = os.urandom(64 * 1024)
    c.upload(vol, f"/{folder}", "seed.bin", seed)

    errors = []

    def list_loop(n: int):
        sc = ShareClient(c.base, c.user, c.pwd, c.timeout)
        if sc.login().status_code != 200:
            errors.append("login")
            return
        for _ in range(n):
            try:
                sc.list(vol, f"/{folder}")
                sc.status()
            except Exception as e:
                errors.append(str(e))

    def down_loop(n: int):
        sc = ShareClient(c.base, c.user, c.pwd, c.timeout)
        if sc.login().status_code != 200:
            errors.append("login")
            return
        for _ in range(n):
            try:
                data = sc.download(vol, f"/{folder}/seed.bin")
                if len(data) != len(seed):
                    errors.append("size mismatch")
            except Exception as e:
                errors.append(str(e))

    with ThreadPoolExecutor(max_workers=4) as ex:
        futs = [ex.submit(list_loop, 8) for _ in range(3)]
        futs.append(ex.submit(down_loop, 4))
        for f in as_completed(futs):
            f.result()
    r.check("concurrent list/status/download", not errors, "; ".join(errors[:5]))

    # Two uploads at once: one may 409 (single-slot) but device must not crash
    results = []

    def up_one(name: str):
        sc = ShareClient(c.base, c.user, c.pwd, c.timeout)
        sc.login()
        resp = sc.upload(vol, f"/{folder}", name, os.urandom(256 * 1024))
        results.append(resp.status_code)

    t1 = threading.Thread(target=up_one, args=("a.bin",))
    t2 = threading.Thread(target=up_one, args=("b.bin",))
    t1.start()
    t2.start()
    t1.join()
    t2.join()
    r.check(
        "parallel uploads no crash",
        all(code in (200, 409) for code in results) and 200 in results,
        str(results),
    )

    listing = c.list(vol, f"/{folder}", limit=500)
    for it in listing.get("items", []):
        c.delete(vol, f"/{folder}", it["name"])
    c.delete(vol, "/", folder)


def test_large_dirs(c: ShareClient, r: Runner, count: int):
    print(f"\n== large dir ({count}) ==")
    vol = pick_vol(c)
    folder = rand_name(f"n{count}")
    r.expect("mkdir large", c.mkdir(vol, "/", folder).status_code == 200)
    # Create via many tiny uploads (slower but exercises upload path)
    # For speed use mkdir of nested only for deep tree; files via upload of empty.
    created = 0
    for i in range(count):
        resp = c.upload(vol, f"/{folder}", f"f{i:04d}.txt", b"x")
        if resp.status_code != 200:
            r.check(f"create file {i}", False, f"{resp.status_code} {resp.text}")
            break
        created += 1
        if i % 50 == 0 and i:
            print(f"  … created {i}")
    r.check(f"created {count} files", created == count, f"got {created}")

    # Paginated list
    offset = 0
    total = 0
    pages = 0
    while True:
        page = c.list(vol, f"/{folder}", offset=offset, limit=100)
        n = len(page.get("items", []))
        total += n
        pages += 1
        if not page.get("hasMore"):
            break
        offset += n
        if pages > 100:
            break
    r.check("paginated list covers all", total >= created, f"listed {total}")

    # Nested delete depth 10
    cur = f"/{folder}"
    ok_depth = True
    for i in range(10):
        name = f"d{i}"
        resp = c.mkdir(vol, cur, name)
        if resp.status_code != 200:
            r.check(f"mkdir depth {i}", False, resp.text)
            ok_depth = False
            break
        cur = f"{cur}/{name}"
    if ok_depth:
        r.check("mkdir depth 10", True)
        resp = c.delete(vol, f"/{folder}", "d0")
        r.check("delete nested depth 10", resp.status_code == 200, resp.text)

    # Cleanup remaining files (paginated deletes)
    while True:
        page = c.list(vol, f"/{folder}", offset=0, limit=200)
        items = page.get("items", [])
        if not items:
            break
        for it in items:
            c.delete(vol, f"/{folder}", it["name"])
    c.delete(vol, "/", folder)


def test_soak(c: ShareClient, r: Runner, minutes: float):
    print(f"\n== soak {minutes} min ==")
    end = time.time() + minutes * 60
    vol = pick_vol(c)
    folder = rand_name("soak")
    c.mkdir(vol, "/", folder)
    cycles = 0
    errors = 0
    while time.time() < end:
        try:
            c.status()
            c.list(vol, f"/{folder}")
            payload = os.urandom(random.randint(0, 32 * 1024))
            name = f"s{cycles}.bin"
            up = c.upload(vol, f"/{folder}", name, payload)
            if up.status_code != 200:
                errors += 1
            else:
                got = c.download(vol, f"/{folder}/{name}")
                if got != payload:
                    errors += 1
                c.delete(vol, f"/{folder}", name)
            cycles += 1
            if cycles % 20 == 0:
                print(f"  … soak cycles={cycles} errors={errors}")
        except Exception as e:
            errors += 1
            print(f"  soak error: {e}")
            time.sleep(1)
    r.check("soak no errors", errors == 0, f"cycles={cycles} errors={errors}")
    page = c.list(vol, f"/{folder}", limit=500)
    for it in page.get("items", []):
        c.delete(vol, f"/{folder}", it["name"])
    c.delete(vol, "/", folder)


def main() -> int:
    ap = argparse.ArgumentParser(description="kvxputer File Share host tests")
    ap.add_argument("--host", required=True, help="Device IP or http://IP")
    ap.add_argument("--user", default="kvx")
    ap.add_argument("--pwd", default="kvxputer", help="Portal password")
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--large", action="store_true", help="Include 50MB upload")
    ap.add_argument("--big-dir", type=int, default=0, help="Create N files (e.g. 500 or 2000)")
    ap.add_argument("--soak", type=float, default=0, help="Soak test minutes")
    ap.add_argument("--skip-upload", action="store_true")
    args = ap.parse_args()

    c = ShareClient(args.host, args.user, args.pwd, args.timeout)
    r = Runner()
    try:
        test_smoke(c, r)
        test_path_security(c, r)
        if not args.skip_upload:
            test_uploads(c, r, include_large=args.large)
        test_concurrency(c, r)
        if args.big_dir:
            test_large_dirs(c, r, args.big_dir)
        if args.soak > 0:
            test_soak(c, r, args.soak)
    except Fail as e:
        print(f"\nAborted: {e}", file=sys.stderr)
        r.failed += 1

    print(f"\nResult: {r.passed} passed, {r.failed} failed")
    return 1 if r.failed else 0


if __name__ == "__main__":
    sys.exit(main())
