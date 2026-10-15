#!/usr/bin/env python3
"""Light source tests using an existing configured QEMU ARM system build.

Does not invoke Ninja, configure, the engine, a guest or a device. The codec
harness uses actual target/TCG headers and only replaces infrastructure allocation
and QOM-name lookup; its integer oracle is a unit check, never guest acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compile-commands", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    commands = json.loads(args.compile_commands.read_text())
    item = next(x for x in commands
                if x["file"].endswith("/target/arm/tcg/translate.c"))
    old_relative = item["file"][:-len("/target/arm/tcg/translate.c")]
    old_absolute = str((Path(item["directory"]) / old_relative).resolve())
    base, skip = [], False
    for word in shlex.split(item["command"]):
        if skip:
            skip = False
            continue
        if word in ("-MQ", "-MF", "-o"):
            skip = True
            continue
        if word in ("-MD", "-c") or word == item["file"]:
            continue
        word = word.replace(old_absolute, str(source))
        word = word.replace(old_relative, str(source))
        # Some captured compdbs retain shell quotes around -D arguments.
        if word.startswith("'") and word.endswith("'"):
            word = word[1:-1]
        base.append(word)
    records = []

    def run(label, command, cwd=item["directory"]):
        result = subprocess.run(command, cwd=cwd, text=True,
                                capture_output=True, timeout=45)
        (output / (label + ".stdout")).write_text(result.stdout)
        (output / (label + ".stderr")).write_text(result.stderr)
        records.append({"label": label, "command": command, "cwd": cwd,
                        "exitCode": result.returncode})
        (output / "commands.json").write_text(json.dumps(records, indent=2))
        if result.returncode:
            raise RuntimeError(label + " failed; inspect retained stderr")

    for filename in ("target/arm/tcg/ir-cache.c", "target/arm/tcg/translate.c",
                     "tcg/ir-cache-store.c"):
        run("syntax-" + Path(filename).stem,
            base + ["-fsyntax-only", str(source / filename)])
    libs = shlex.split(subprocess.check_output(
        ["pkg-config", "--libs", "glib-2.0"], text=True))
    for name, test in (("store", "test-tcg-ir-cache-store.c"),
                       ("codec", "test-arm-tcg-ir-cache.c")):
        extra = ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"]
        if args.sanitize:
            extra += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        run(name + "-compile", base + extra + ["-o", str(output / name),
            str(source / "tests/unit" / test),
            str(source / "tcg/ir-cache-store.c")] + libs + ["-ldl"])
        run(name + "-tests", [str(output / name)])
    with tempfile.TemporaryDirectory(prefix="lighttouch-ir-process-") as d:
        Path(d).chmod(0o700)
        run("fresh-producer", [str(output / "store"), "--produce", d])
        run("fresh-consumer", [str(output / "store"), "--consume", d])
    with tempfile.TemporaryDirectory(prefix="lighttouch-ir-hook-process-") as d:
        Path(d).chmod(0o700)
        run("disabled-by-default", [str(output / "codec"), "--disabled", d])
        if list(Path(d).iterdir()):
            raise RuntimeError("Disabled cache created files")
        run("fresh-ir-producer", [str(output / "codec"), "--produce-ir", d])
        run("fresh-ir-consumer", [str(output / "codec"), "--consume-ir", d])
    files = ("target/arm/tcg/ir-cache.c", "target/arm/tcg/ir-cache.h",
             "include/tcg/ir-cache-store.h", "tcg/ir-cache-store.c",
             "tests/unit/test-arm-tcg-ir-cache.c",
             "tests/unit/test-tcg-ir-cache-store.c")
    result = {"scope": "source syntax and isolated codec/store units only",
              "guestAcceptance": False, "performanceQualified": False,
              "sanitizers": args.sanitize, "result": "PASS",
              "files": {f: hashlib.sha256((source / f).read_bytes()).hexdigest()
                        for f in files}, "commandCount": len(records)}
    (output / "RESULT.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
