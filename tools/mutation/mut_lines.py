"""지정한 파일/줄의 변이만 다시 돌린다(생존 변이가 새 테스트로 잡히는지 확인). 사용: python mut_lines.py <clone> <file> <line,line,...>"""
import os
import re
import subprocess
import sys

clone, rel, wanted = sys.argv[1], sys.argv[2], {int(x) for x in sys.argv[3].split(",")}
path = os.path.join(clone, rel)
src = open(path, encoding="utf-8", newline="").read()
nl = "\r\n" if "\r\n" in src else "\n"
lines = src.split(nl)
env = dict(os.environ)
tmp = os.path.join(clone, "_tmp")
env["TMP"] = env["TEMP"] = tmp

OPS = [(" * ", " / "), (" + ", " - "), (" - ", " + "), ("<", "<="), (">", ">="), ("==", "!="), ("||", "&&")]
for ln in sorted(wanted):
    line = lines[ln - 1]
    code = line.split("//")[0]
    for old, new in OPS:
        if old.strip() in ("<", ">") and ("<float" in code or "<double" in code or "<uint" in code or "<int" in code):
            continue
        for m in re.finditer(re.escape(old), code):
            mutated = code[:m.start()] + new + code[m.end():] + line[len(code):]
            nl_lines = list(lines)
            nl_lines[ln - 1] = mutated
            open(path, "w", encoding="utf-8", newline="").write(nl.join(nl_lines))
            try:
                b = subprocess.run(["cmake", "--build", "build", "--config", "Release", "--target", "engine_tests"], cwd=clone, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
                if b.returncode != 0:
                    status = "invalid"
                else:
                    t = subprocess.run([os.path.join(clone, "build", "bin", "engine_tests.exe")], cwd=clone, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
                    status = "SURVIVED" if t.returncode == 0 else "killed"
            finally:
                open(path, "w", encoding="utf-8", newline="").write(src)
            print(f"{rel}:{ln} [{old.strip()} -> {new.strip()}] col {m.start()} -> {status}", flush=True)
