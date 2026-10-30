import glob
import json
import sys
from collections import Counter, defaultdict


def load(pattern):
    rows = []
    for f in glob.glob(pattern):
        rows += json.load(open(f, encoding="utf-8"))
    return rows


r1 = load(r"F:\gge_mut\*\mut_out_round1.json")
r2 = load(r"F:\gge_mut\*\mut_out.json")


def summarize(rows):
    per = defaultdict(Counter)
    for r in rows:
        per[r["file"]][r["status"]] += 1
    return per


p1, p2 = summarize(r1), summarize(r2)
print(f"{'file':28s} {'round1 killed/valid':>22s} {'round2 killed/valid':>22s}")
tot1 = Counter()
tot2 = Counter()
for f in sorted(set(p1) | set(p2)):
    a, b = p1[f], p2[f]
    va, vb = a["killed"] + a["survived"], b["killed"] + b["survived"]
    tot1.update(a)
    tot2.update(b)
    print(f"{f.split('/')[-1]:28s} {a['killed']:4d}/{va:<4d} {100 * a['killed'] / max(1, va):5.1f}%   {b['killed']:4d}/{vb:<4d} {100 * b['killed'] / max(1, vb):5.1f}%")
va, vb = tot1["killed"] + tot1["survived"], tot2["killed"] + tot2["survived"]
print(f"{'TOTAL':28s} {tot1['killed']:4d}/{va:<4d} {100 * tot1['killed'] / va:5.1f}%   {tot2['killed']:4d}/{vb:<4d} {100 * tot2['killed'] / vb:5.1f}%")
print("invalid:", tot1["invalid"], tot2["invalid"])

if len(sys.argv) > 1:
    only = sys.argv[1].split(",")
    sv = sorted((r for r in r2 if r["status"] == "survived" and r["file"].split("/")[-1] in only), key=lambda r: (r["file"], r["line"]))
    for r in sv:
        print(f"{r['file'].split('/')[-1]}:{r['line']:4d} [{r['op']}] {r['orig'][:105]}")
