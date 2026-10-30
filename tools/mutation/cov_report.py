import glob
import os
import sys
import xml.etree.ElementTree as ET

# 사용: python cov_report.py <결과 폴더>  (폴더 아래 cov_interop, cov_ui에 `dotnet test --collect:"XPlat Code Coverage" --results-directory` 결과를 둔다)
SP = sys.argv[1] if len(sys.argv) > 1 else "."


def load(pattern):
    files = glob.glob(pattern, recursive=True)
    assert files, pattern
    root = ET.parse(files[0]).getroot()
    result = {}
    for cls in root.iter("class"):
        fn = cls.get("filename")
        d = result.setdefault(fn, {"lines": {}, "branches": [0, 0]})
        for line in cls.find("lines").iter("line"):
            n = int(line.get("number"))
            d["lines"][n] = max(d["lines"].get(n, 0), int(line.get("hits")))
            if line.get("branch") == "True":
                cov = line.get("condition-coverage", "")
                if "(" in cov:
                    a, b = cov.split("(")[1].rstrip(")").split("/")
                    d["branches"][0] += int(a)
                    d["branches"][1] += int(b)
    return result


def merge(a, b):
    out = {k: {"lines": dict(v["lines"]), "branches": list(v["branches"])} for k, v in a.items()}
    for k, v in b.items():
        d = out.setdefault(k, {"lines": {}, "branches": [0, 0]})
        for n, h in v["lines"].items():
            d["lines"][n] = max(d["lines"].get(n, 0), h)
    return out


def norm(fn):
    fn = fn.replace("/", "\\")
    for marker in ("GunGame.Interop\\", "GunGameEditor\\"):
        if marker in fn:
            return marker + fn.split(marker, 1)[1]
    return "GunGame.Interop\\" + fn   # 상대 경로로 기록된 Interop 소스


def renorm(d):
    out = {}
    for k, v in d.items():
        t = out.setdefault(norm(k), {"lines": {}, "branches": [0, 0]})
        for n, h in v["lines"].items():
            t["lines"][n] = max(t["lines"].get(n, 0), h)
    return out


interop = renorm(load(SP + r"\cov_interop\**\coverage.cobertura.xml"))
ui = renorm(load(SP + r"\cov_ui\**\coverage.cobertura.xml"))
merged = merge(interop, ui)

print(f"{'file':60s} {'lines':>12s} {'cov%':>6s}  uncovered lines")
total_c = total_l = 0
for fn in sorted(merged):
    if any(x in fn for x in ("\\obj\\", "Tests", ".g.cs", "AssemblyInfo")):
        continue
    d = merged[fn]
    lines = d["lines"]
    covered = sum(1 for h in lines.values() if h > 0)
    total_c += covered
    total_l += len(lines)
    unc = sorted(n for n, h in lines.items() if h == 0)
    # 연속 구간으로 압축
    ranges = []
    for n in unc:
        if ranges and n == ranges[-1][1] + 1:
            ranges[-1][1] = n
        else:
            ranges.append([n, n])
    text = ",".join(f"{a}" if a == b else f"{a}-{b}" for a, b in ranges)
    short = fn.split("GunGameEngine\\tools\\")[-1] if "GunGameEngine" in fn else fn
    print(f"{short:60s} {covered:5d}/{len(lines):<5d} {100.0 * covered / max(1, len(lines)):5.1f}%  {text[:110]}")
print(f"TOTAL {total_c}/{total_l} = {100.0 * total_c / total_l:.1f}%")
