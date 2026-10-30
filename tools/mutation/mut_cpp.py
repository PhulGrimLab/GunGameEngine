"""C++ 엔진 뮤테이션 테스트 하네스.

사용: python mut_cpp.py <clone_dir> <shard> <shards> <out_json> <file1,file2,...>
각 소스 파일의 코드 줄에서 연산자를 하나씩 바꾼 변이(mutant)를 만들고, 빌드 후 engine_tests.exe를 돌려
테스트가 실패하면 killed, 모두 통과하면 survived로 기록한다. 컴파일이 안 되는 변이는 invalid로 센다.
"""
import json
import os
import re
import subprocess
import sys
import time

clone, shard, shards, out_json, files = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], sys.argv[5].split(",")

OPS = [
    ("==", "!="), ("!=", "=="), ("<=", "<"), (">=", ">"), ("&&", "||"), ("||", "&&"),
    (" < ", " <= "), (" > ", " >= "), (" + ", " - "), (" - ", " + "), (" * ", " / "),
    ("true", "false"), ("false", "true"),
]


def code_part(line):
    """줄에서 // 주석 앞부분만 돌려준다(문자열 안의 //는 고려하지 않는 단순 처리)."""
    i = line.find("//")
    return line if i < 0 else line[:i]


def in_string(text, pos):
    return text[:pos].count('"') % 2 == 1


def generate(path):
    src = open(path, encoding="utf-8", newline="").read()
    lines = src.split("\n")
    mutants = []
    in_raw = False
    for ln, line in enumerate(lines):
        if 'R"' in line and ')"' not in line:
            in_raw = True
            continue
        if in_raw:
            if ')"' in line:
                in_raw = False
            continue
        stripped = line.strip()
        if not stripped or stripped.startswith(("//", "#", "*", "/*", "static_assert")):
            continue
        code = code_part(line)
        for old, new in OPS:
            for m in re.finditer(re.escape(old), code):
                if in_string(code, m.start()):
                    continue
                if old in ("true", "false") and (code[m.start() - 1:m.start()].isalnum() or code[m.end():m.end() + 1].isalnum() or code[m.end():m.end() + 1] == "_"):
                    continue
                mutated = code[:m.start()] + new + code[m.end():] + line[len(code):]
                mutants.append((ln, line, mutated, f"{old.strip()} -> {new.strip()}"))
    return src, lines, mutants


def run(cmd, cwd, env, timeout):
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, timeout=timeout, encoding="utf-8", errors="replace")


env = dict(os.environ)
tmp = os.path.join(clone, "_tmp")
os.makedirs(tmp, exist_ok=True)
env["TMP"] = tmp
env["TEMP"] = tmp

results = []
done = 0
for rel in files:
    path = os.path.join(clone, rel)
    src, lines, mutants = generate(path)
    for idx, (ln, line, mutated, desc) in enumerate(mutants):
        if (idx + sum(map(ord, rel)) % 7) % shards != shard:  # 파일마다 샤드 분배가 한쪽으로 쏠리지 않게 한다
            continue
        new_lines = list(lines)
        new_lines[ln] = mutated
        open(path, "w", encoding="utf-8", newline="").write("\n".join(new_lines))
        status, detail = "survived", ""
        try:
            b = run(["cmake", "--build", "build", "--config", "Release", "--target", "engine_tests"], clone, env, 600)
            if b.returncode != 0:
                status = "invalid"
            else:
                t = run([os.path.join(clone, "build", "bin", "engine_tests.exe")], clone, env, 180)
                if t.returncode != 0:
                    status = "killed"
                    detail = ",".join(re.findall(r"\[FAIL\] (\S+)", t.stdout))
        except subprocess.TimeoutExpired:
            status = "killed"  # 무한 루프/정지는 테스트가 감지한 것으로 본다
            detail = "timeout"
        finally:
            open(path, "w", encoding="utf-8", newline="").write(src)
        results.append({"file": rel, "line": ln + 1, "orig": line.strip(), "mut": mutated.strip(), "op": desc, "status": status, "by": detail})
        done += 1
        json.dump(results, open(out_json, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        print(f"[{shard}] {done} {rel}:{ln + 1} {desc} -> {status}", flush=True)
print(f"[{shard}] finished {done}")
