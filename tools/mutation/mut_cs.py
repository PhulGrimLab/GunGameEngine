"""C# 라이브러리 뮤테이션 테스트 하네스.

사용: python mut_cs.py <clone_dir> <shard> <shards> <out_json> <tests_project> <file1,file2,...>
clone_dir\tools 아래의 소스 파일에서 연산자를 하나씩 바꾼 변이를 만들고 `dotnet test <tests_project>`를 돌려
실패하면 killed, 모두 통과하면 survived, 컴파일 오류면 invalid로 기록한다.
(일괄 실행에서 증분 빌드 때문에 생존이 거짓으로 나올 수 있으므로, 생존 판정은 따로 다시 확인해야 한다.)
"""
import json
import os
import re
import subprocess
import sys

clone, shard, shards, out_json, tests_project, files = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], sys.argv[5], sys.argv[6].split(",")
tools = os.path.join(clone, "tools")

OPS = [
    ("==", "!="), ("!=", "=="), ("<=", "<"), (">=", ">"), ("&&", "||"), ("||", "&&"),
    (" < ", " <= "), (" > ", " >= "), (" + ", " - "), (" - ", " + "), (" * ", " / "),
    ("true", "false"), ("false", "true"),
]


def code_part(line):
    i = line.find("//")
    return line if i < 0 else line[:i]


def in_string(text, pos):
    return text[:pos].count('"') % 2 == 1


def generate(path):
    src = open(path, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in src else "\n"
    lines = src.split(nl)
    mutants = []
    in_raw = False
    for ln, line in enumerate(lines):
        stripped = line.strip()
        if '"""' in stripped:
            in_raw = not in_raw if stripped.count('"""') == 1 else in_raw
            continue
        if in_raw or not stripped or stripped.startswith(("//", "///", "[", "using ", "namespace ")):
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
    return src, nl, lines, mutants


def run_tests(timeout):
    cmd = ["dotnet", "test", tests_project, "--nologo"]
    p = subprocess.Popen(cmd, cwd=tools, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    try:
        out, _ = p.communicate(timeout=timeout)
        return p.returncode, out, False
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)], capture_output=True)
        out, _ = p.communicate()
        return -1, out or "", True


results = []
done = 0
for rel in files:
    path = os.path.join(tools, rel.replace("/", os.sep))
    src, nl, lines, mutants = generate(path)
    for idx, (ln, line, mutated, desc) in enumerate(mutants):
        if (idx + sum(map(ord, rel)) % 7) % shards != shard:
            continue
        new_lines = list(lines)
        new_lines[ln] = mutated
        open(path, "w", encoding="utf-8", newline="").write(nl.join(new_lines))
        status, detail = "survived", ""
        try:
            rc, out, timed_out = run_tests(240)
            if timed_out:
                status, detail = "killed", "timeout"
            elif "error CS" in out or "오류 CS" in out:
                status = "invalid"
            elif rc != 0:
                status = "killed"
                detail = ",".join(re.findall(r"^\s+실패 (\S+)", out, flags=re.M))[:300]
        finally:
            open(path, "w", encoding="utf-8", newline="").write(src)
        results.append({"file": rel, "line": ln + 1, "orig": line.strip(), "mut": mutated.strip(), "op": desc, "status": status, "by": detail})
        done += 1
        json.dump(results, open(out_json, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        print(f"[{shard}] {done} {rel}:{ln + 1} {desc} -> {status}", flush=True)
print(f"[{shard}] finished {done}")
