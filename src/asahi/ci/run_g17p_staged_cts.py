#!/usr/bin/env python3
"""Fail-fast Vulkan CTS promotion runner for first G17P bring-up."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import gzip
import hashlib
import json
import os
import re
import shlex
import shutil
import signal
import stat
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_CONFIG = SCRIPT_DIR / "g17p-cts-stages.json"
FORBIDDEN_CAP_ENV = ("MESA_VK_VERSION_OVERRIDE", "MESA_EXTENSION_OVERRIDE")


class BringupError(RuntimeError):
   pass


def read_json(path: Path) -> dict[str, Any]:
   try:
      with path.open("r", encoding="utf-8") as stream:
         value = json.load(stream)
   except (OSError, json.JSONDecodeError) as exc:
      raise BringupError(f"cannot read {path}: {exc}") from exc
   if not isinstance(value, dict):
      raise BringupError(f"{path}: top level must be an object")
   return value


def validate_config(config: dict[str, Any]) -> None:
   if config.get("schema") != 1:
      raise BringupError("unsupported stage-config schema")

   for key in ("device_regex", "driver_regex", "drm_driver_regex",
               "kernel_fault_regex"):
      try:
         re.compile(config[key])
      except (KeyError, TypeError, re.error) as exc:
         raise BringupError(f"invalid {key}: {exc}") from exc

   stages = config.get("stages")
   if not isinstance(stages, list) or not stages:
      raise BringupError("stage config has no stages")

   names: set[str] = set()
   for stage in stages:
      if not isinstance(stage, dict):
         raise BringupError("each stage must be an object")
      name = stage.get("name")
      if not isinstance(name, str) or not re.fullmatch(r"[a-z][a-z0-9_-]*", name):
         raise BringupError(f"invalid stage name: {name!r}")
      if name in names:
         raise BringupError(f"duplicate stage: {name}")
      names.add(name)
      if not isinstance(stage.get("gate"), str):
         raise BringupError(f"stage {name}: missing gate")
      if not isinstance(stage.get("timeout_seconds"), int) or stage["timeout_seconds"] < 1:
         raise BringupError(f"stage {name}: invalid timeout")
      groups = stage.get("tests_per_group")
      if not isinstance(groups, int) or not 1 <= groups <= 10000:
         raise BringupError(f"stage {name}: tests_per_group must be within 1..10000")
      selectors = stage.get("selectors")
      if not isinstance(selectors, list) or not selectors:
         raise BringupError(f"stage {name}: no selectors")
      for selector in selectors:
         try:
            re.compile(selector["regex"])
            minimum = selector["minimum"]
            limit = selector["limit"]
         except (KeyError, TypeError, re.error) as exc:
            raise BringupError(f"stage {name}: invalid selector: {exc}") from exc
         if not isinstance(minimum, int) or minimum < 1:
            raise BringupError(f"stage {name}: selector minimum must be positive")
         if not isinstance(limit, int) or limit < 0 or (limit and limit < minimum):
            raise BringupError(f"stage {name}: selector limit is inconsistent")

   required = ["loader", "compute", "triangle", "expanded", "full"]
   if [stage["name"] for stage in stages] != required:
      raise BringupError(f"stages must be ordered exactly as {required}")
   if not stages[-1].get("requires_full_opt_in"):
      raise BringupError("the full stage must require explicit opt-in")


def scan_mustpass(path: Path, patterns: Iterable[str]) -> tuple[list[str], int]:
   matchers = [re.compile(pattern) for pattern in patterns]
   candidates: set[str] = set()
   count = 0

   def consume(lines: Iterable[str]) -> None:
      nonlocal count
      for line in lines:
         case = line.strip()
         if not case.startswith("dEQP-VK."):
            continue
         count += 1
         if any(matcher.search(case) for matcher in matchers):
            candidates.add(case)

   try:
      if path.suffix == ".zst":
         process = subprocess.Popen(
            ["zstd", "-q", "-dc", str(path)], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, encoding="utf-8",
            errors="replace",
         )
         assert process.stdout is not None
         consume(process.stdout)
         process.stdout.close()
         stderr = process.stderr.read() if process.stderr is not None else ""
         if process.wait() != 0:
            raise BringupError(f"zstd could not decompress {path}: {stderr.strip()}")
      else:
         opener = gzip.open if path.suffix == ".gz" else open
         with opener(path, "rt", encoding="utf-8", errors="replace") as stream:
            consume(stream)
   except (OSError, subprocess.SubprocessError) as exc:
      raise BringupError(f"cannot read must-pass list {path}: {exc}") from exc
   if count == 0:
      raise BringupError(f"must-pass list {path} contains no Vulkan CTS cases")
   return sorted(candidates), count


def select_cases(all_cases: Iterable[str], stage: dict[str, Any],
                 prior: set[str]) -> tuple[list[str], list[dict[str, Any]]]:
   allow_prior = bool(stage.get("rerun_prior"))
   selected: list[str] = []
   selected_set: set[str] = set()
   selector_results: list[dict[str, Any]] = []

   for selector in stage["selectors"]:
      matcher = re.compile(selector["regex"])
      matches = [case for case in all_cases
                 if matcher.search(case) and case not in selected_set and
                 (allow_prior or case not in prior)]
      limit = selector["limit"]
      chosen = matches if limit == 0 else matches[:limit]
      if len(chosen) < selector["minimum"]:
         raise BringupError(
            f"stage {stage['name']} selector {selector.get('name', selector['regex'])} "
            f"requires {selector['minimum']} cases, found {len(chosen)}"
         )
      selected.extend(chosen)
      selected_set.update(chosen)
      selector_results.append({
         "name": selector.get("name", selector["regex"]),
         "regex": selector["regex"],
         "available": len(matches),
         "selected": len(chosen),
      })

   return selected, selector_results


def toml_string(value: str) -> str:
   return json.dumps(value, ensure_ascii=True)


def write_stage_files(stage_dir: Path, deqp_vk: str, tests_per_group: int,
                      cases: list[str] | None,
                      source_caselist: Path | None = None) -> None:
   stage_dir.mkdir(parents=True, exist_ok=False)
   if source_caselist is not None:
      caselist = source_caselist.resolve()
      write_json(stage_dir / "caselist-source.json", {
         "path": str(caselist),
         "sha256": sha256_file(caselist),
         "size": caselist.stat().st_size,
      })
   else:
      assert cases is not None
      caselist = stage_dir / "caselist.txt"
      caselist.write_text("".join(f"{case}\n" for case in cases), encoding="utf-8")
   suite = (
      "[[deqp]]\n"
      f"deqp = {toml_string(deqp_vk)}\n"
      f"caselists = [{toml_string(str(caselist.resolve()))}]\n"
      f"tests_per_group = {tests_per_group}\n"
   )
   (stage_dir / "suite.toml").write_text(suite, encoding="utf-8")


def build_plan(config: dict[str, Any], candidate_cases: list[str], total_cases: int,
               mustpass: Path, through: str, allow_full: bool, output: Path,
               deqp_vk: str) -> dict[str, Any]:
   names = [stage["name"] for stage in config["stages"]]
   if through not in names:
      raise BringupError(f"unknown final stage {through!r}; choose one of {names}")
   if through == "full" and not allow_full:
      raise BringupError("full CTS requires --allow-full-cts")

   prior: set[str] = set()
   plan_stages: list[dict[str, Any]] = []
   for number, stage in enumerate(config["stages"][:names.index(through) + 1]):
      stage_dir = output / f"{number:02d}-{stage['name']}"
      if stage.get("rerun_prior"):
         cases = None
         selector_results = [{
            "name": "vk-main", "regex": "^dEQP-VK\\.",
            "available": total_cases, "selected": total_cases,
         }]
         case_count = total_cases
         write_stage_files(stage_dir, deqp_vk, stage["tests_per_group"], None,
                           mustpass)
      else:
         cases, selector_results = select_cases(candidate_cases, stage, prior)
         case_count = len(cases)
         write_stage_files(stage_dir, deqp_vk, stage["tests_per_group"], cases)
      plan_stages.append({
         "number": number,
         "name": stage["name"],
         "gate": stage["gate"],
         "timeout_seconds": stage["timeout_seconds"],
         "tests_per_group": stage["tests_per_group"],
         "case_count": case_count,
         "selectors": selector_results,
         "directory": str(stage_dir.resolve()),
      })
      if cases is not None:
         prior.update(cases)

   return {
      "schema": 1,
      "target": config.get("target", "Apple A18 Pro / G17P"),
      "through": through,
      "full_cts_opt_in": allow_full,
      "mustpass_case_count": total_cases,
      "stages": plan_stages,
   }


def command_path(command: str) -> str | None:
   if os.sep in command:
      path = Path(command)
      return str(path) if path.is_file() and os.access(path, os.X_OK) else None
   return shutil.which(command)


def run_logged(command: list[str], log_path: Path, timeout: int,
               env: dict[str, str] | None = None) -> dict[str, Any]:
   started = time.monotonic()
   started_utc = dt.datetime.now(dt.timezone.utc).isoformat()
   timed_out = False
   log_path.parent.mkdir(parents=True, exist_ok=True)
   with log_path.open("w", encoding="utf-8", errors="replace") as log:
      log.write(f"$ {shlex.join(command)}\n")
      log.flush()
      try:
         process = subprocess.Popen(
            command, stdout=log, stderr=subprocess.STDOUT, text=True,
            env=env, start_new_session=True,
         )
      except OSError as exc:
         log.write(f"exec failed: {exc}\n")
         return {
            "command": command, "exit_code": None, "timed_out": False,
            "duration_seconds": round(time.monotonic() - started, 3),
            "started_utc": started_utc, "error": str(exc),
         }
      try:
         exit_code = process.wait(timeout=timeout)
      except subprocess.TimeoutExpired:
         timed_out = True
         log.write(f"timeout after {timeout} seconds; terminating process group\n")
         log.flush()
         try:
            os.killpg(process.pid, signal.SIGTERM)
         except ProcessLookupError:
            pass
         try:
            exit_code = process.wait(timeout=5)
         except subprocess.TimeoutExpired:
            try:
               os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
               pass
            exit_code = process.wait()
   return {
      "command": command,
      "exit_code": exit_code,
      "timed_out": timed_out,
      "duration_seconds": round(time.monotonic() - started, 3),
      "started_utc": started_utc,
   }


class KernelLogMonitor:
   """Continuously capture new kernel messages without relying on ring retention."""

   def __init__(self, path: Path, command: list[str] | None = None):
      self.path = path
      self.command = command or ["dmesg", "--follow-new", "--color=never"]
      self.process: subprocess.Popen[bytes] | None = None
      self.log: Any = None
      self.read_offset = 0
      self.started = time.monotonic()
      self.started_utc = dt.datetime.now(dt.timezone.utc).isoformat()

   def start(self) -> dict[str, Any]:
      self.path.parent.mkdir(parents=True, exist_ok=True)
      self.log = self.path.open("wb")
      self.log.write(f"$ {shlex.join(self.command)}\n".encode("utf-8"))
      self.log.flush()
      self.read_offset = self.log.tell()
      try:
         self.process = subprocess.Popen(
            self.command, stdout=self.log, stderr=subprocess.STDOUT,
         )
      except OSError as exc:
         self.log.write(f"exec failed: {exc}\n".encode("utf-8", errors="replace"))
         self.log.close()
         raise BringupError(f"kernel monitor: cannot start: {exc}") from exc
      try:
         exit_code = self.process.wait(timeout=0.1)
      except subprocess.TimeoutExpired:
         pass
      else:
         self.log.flush()
         self.log.close()
         raise BringupError(
            f"kernel monitor: exited during startup with code {exit_code}; "
            f"see {self.path}"
         )
      return {
         "command": self.command,
         "log": str(self.path.resolve()),
         "started_utc": self.started_utc,
         "status": "running",
      }

   def read_new_lines(self, final: bool = False) -> list[str]:
      try:
         with self.path.open("rb") as stream:
            stream.seek(self.read_offset)
            data = stream.read()
      except OSError as exc:
         raise BringupError(f"kernel monitor: cannot read {self.path}: {exc}") from exc

      if not final:
         last_newline = data.rfind(b"\n")
         if last_newline < 0:
            return []
         data = data[:last_newline + 1]
      self.read_offset += len(data)
      return data.decode("utf-8", errors="replace").splitlines()

   def require_alive(self, gate: str) -> None:
      assert self.process is not None
      exit_code = self.process.poll()
      if exit_code is not None:
         raise BringupError(
            f"{gate}: continuous kernel monitor exited early with code {exit_code}"
         )

   def stop(self) -> tuple[dict[str, Any], list[str]]:
      assert self.process is not None
      premature = self.process.poll() is not None
      if not premature:
         try:
            self.process.terminate()
         except ProcessLookupError:
            pass
         try:
            self.process.wait(timeout=5)
         except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
      if self.log is not None and not self.log.closed:
         self.log.flush()
         self.log.close()
      tail = self.read_new_lines(final=True)
      return ({
         "command": self.command,
         "log": str(self.path.resolve()),
         "started_utc": self.started_utc,
         "duration_seconds": round(time.monotonic() - self.started, 3),
         "exit_code": self.process.returncode,
         "premature_exit": premature,
         "status": "failed" if premature else "stopped",
      }, tail)


def dmesg_snapshot(path: Path) -> dict[str, Any]:
   return run_logged(["dmesg", "--color=never"], path, 30)


def dmesg_payload(path: Path) -> list[str]:
   lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
   return lines[1:] if lines and lines[0].startswith("$ ") else lines


def new_dmesg_lines(before: list[str], after: list[str]) -> list[str]:
   if len(after) >= len(before) and after[:len(before)] == before:
      return after[len(before):]
   # The ring buffer wrapped or changed under us. Scanning all of it is safer.
   return after


def matching_faults(lines: Iterable[str], pattern: str) -> list[str]:
   matcher = re.compile(pattern)
   return [line for line in lines if matcher.search(line)]


def inspect_drm(render_node: Path, driver_pattern: str) -> dict[str, Any]:
   try:
      mode = render_node.stat().st_mode
   except OSError as exc:
      raise BringupError(f"DRM gate: cannot stat {render_node}: {exc}") from exc
   if not stat.S_ISCHR(mode):
      raise BringupError(f"DRM gate: {render_node} is not a character device")

   driver_link = Path("/sys/class/drm") / render_node.name / "device/driver"
   try:
      resolved = driver_link.resolve(strict=True)
   except OSError as exc:
      raise BringupError(f"DRM gate: cannot resolve {driver_link}: {exc}") from exc
   driver = resolved.name
   if not re.search(driver_pattern, driver):
      raise BringupError(
         f"DRM gate: expected driver /{driver_pattern}/, found {driver!r}"
      )
   return {
      "render_node": str(render_node.resolve()),
      "sysfs_driver": str(resolved),
      "driver": driver,
   }


def capture_host(output: Path, render_node: Path) -> dict[str, Any]:
   host_dir = output / "host"
   host_dir.mkdir(exist_ok=True)
   commands = {
      "uname": ["uname", "-a"],
      "kernel_cmdline": ["cat", "/proc/cmdline"],
      "render_node": ["ls", "-l", str(render_node)],
      "asahi_module": ["modinfo", "asahi"],
   }
   return {
      name: run_logged(command, host_dir / f"{name}.log", 30)
      for name, command in commands.items()
   }


def write_json(path: Path, value: Any) -> None:
   path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                   encoding="utf-8")


def validate_proof_stage_results(stage_dir: Path,
                                 expected_count: int) -> dict[str, Any]:
   """Require one exact Pass result for every selected bring-up proof case."""
   caselist = stage_dir / "caselist.txt"
   results = stage_dir / "results/results.csv"
   report_path = stage_dir / "result-validation.json"
   problems: list[str] = []

   try:
      expected = [line.strip() for line in caselist.read_text(
         encoding="utf-8", errors="strict").splitlines() if line.strip()]
   except (OSError, UnicodeError) as exc:
      raise BringupError(
         f"{stage_dir.name} result gate: cannot read selected cases: {exc}"
      ) from exc
   expected_set = set(expected)
   if len(expected) != expected_count:
      problems.append(
         f"caselist has {len(expected)} cases, plan expected {expected_count}"
      )
   if len(expected_set) != len(expected):
      problems.append("caselist contains duplicate cases")

   observed: dict[str, str] = {}
   duplicate_results: list[str] = []
   malformed_rows: list[int] = []
   try:
      with results.open("r", encoding="utf-8", errors="strict", newline="") as stream:
         for row_number, row in enumerate(csv.reader(stream), start=1):
            if len(row) != 3:
               malformed_rows.append(row_number)
               continue
            case, status, duration = row
            try:
               float(duration)
            except ValueError:
               malformed_rows.append(row_number)
               continue
            if case in observed:
               duplicate_results.append(case)
            else:
               observed[case] = status
   except (OSError, UnicodeError, csv.Error) as exc:
      problems.append(f"cannot read results.csv: {exc}")

   observed_set = set(observed)
   missing = sorted(expected_set - observed_set)
   unexpected = sorted(observed_set - expected_set)
   non_pass = sorted(
      {case: observed[case] for case in expected_set & observed_set
       if observed[case] != "Pass"}.items()
   )
   if malformed_rows:
      problems.append(f"malformed result rows: {malformed_rows}")
   if duplicate_results:
      problems.append(f"duplicate result cases: {sorted(set(duplicate_results))}")
   if missing:
      problems.append(f"missing results: {missing}")
   if unexpected:
      problems.append(f"unexpected results: {unexpected}")
   if non_pass:
      problems.append(f"non-Pass results: {non_pass}")

   report = {
      "schema": 1,
      "status": "failed" if problems else "passed",
      "expected_count": expected_count,
      "observed_count": len(observed),
      "pass_count": sum(status == "Pass" for status in observed.values()),
      "problems": problems,
   }
   write_json(report_path, report)
   if problems:
      raise BringupError(
         f"{stage_dir.name} result gate did not prove every selected case passed; "
         f"see {report_path}"
      )
   return report


def artifact_inventory(output: Path) -> list[dict[str, Any]]:
   inventory = []
   for path in sorted(output.rglob("*")):
      if not path.is_file() or path.name == "artifact-manifest.json":
         continue
      inventory.append({
         "path": str(path.relative_to(output)),
         "size": path.stat().st_size,
         "sha256": sha256_file(path),
      })
   return inventory


def sha256_file(path: Path) -> str:
   digest = hashlib.sha256()
   with path.open("rb") as stream:
      for chunk in iter(lambda: stream.read(1024 * 1024), b""):
         digest.update(chunk)
   return digest.hexdigest()


def input_record(path: Path, role: str) -> dict[str, Any]:
   try:
      resolved = path.resolve(strict=True)
      metadata = resolved.stat()
   except OSError as exc:
      raise BringupError(f"cannot record {role} input {path}: {exc}") from exc
   if not stat.S_ISREG(metadata.st_mode):
      raise BringupError(f"{role} input is not a regular file: {resolved}")
   return {
      "role": role,
      "path": str(resolved),
      "size": metadata.st_size,
      "sha256": sha256_file(resolved),
   }


def update_manifest(output: Path, state: dict[str, Any]) -> None:
   manifest = dict(state)
   manifest["artifacts"] = artifact_inventory(output)
   write_json(output / "artifact-manifest.json", manifest)


def check_capability_environment() -> None:
   present = [name for name in FORBIDDEN_CAP_ENV if os.environ.get(name)]
   if present:
      raise BringupError(
         "refusing capability overrides during bring-up: " + ", ".join(present)
      )


def success_marker(through: str) -> str:
   markers = {
      "loader": "A18_GPU_LOADER_PASS",
      "compute": "A18_GPU_COMPUTE_PASS",
      "triangle": "A18_GPU_TRIANGLE_PASS",
      "expanded": "A18_GPU_CTS_SMOKE_PASS",
      "full": "A18_GPU_CTS_FULL_PASS",
   }
   return markers[through]


def parse_args(argv: list[str]) -> argparse.Namespace:
   parser = argparse.ArgumentParser(description=__doc__)
   parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
   parser.add_argument("--mustpass", type=Path,
                       default=Path("/deqp-vk/mustpass/vk-main.txt.zst"))
   parser.add_argument("--deqp-vk",
                       default="/deqp-vk/external/vulkancts/modules/vulkan/deqp-vk")
   parser.add_argument("--deqp-runner", default="deqp-runner")
   parser.add_argument("--vulkaninfo", default="vulkaninfo")
   parser.add_argument("--render-node", type=Path, default=Path("/dev/dri/renderD128"))
   parser.add_argument("--output", type=Path)
   parser.add_argument("--through", default="triangle")
   parser.add_argument("--allow-full-cts", action="store_true")
   parser.add_argument("--allow-unreadable-dmesg", action="store_true")
   parser.add_argument("--jobs", type=int, default=1)
   parser.add_argument("--dry-run", action="store_true")
   return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
   raw_argv = sys.argv[1:] if argv is None else argv
   args = parse_args(raw_argv)
   kernel_monitor: KernelLogMonitor | None = None
   try:
      check_capability_environment()
      config = read_json(args.config)
      validate_config(config)
      if args.jobs < 1 or args.jobs > 4:
         raise BringupError("--jobs must be between 1 and 4; first bring-up should use 1")
      stage_names = [stage["name"] for stage in config["stages"]]
      if args.through not in stage_names:
         raise BringupError(
            f"unknown final stage {args.through!r}; choose one of {stage_names}"
         )
      if args.through == "full" and not args.allow_full_cts:
         raise BringupError("full CTS requires --allow-full-cts")

      artifact_env = os.environ.get("A18_GPU_ARTIFACT_DIR")
      output = args.output
      if artifact_env:
         env_output = Path(artifact_env).resolve()
         if output is not None and output.resolve() != env_output:
            raise BringupError(
               "--output must match A18_GPU_ARTIFACT_DIR when the ladder sets it"
            )
         output = env_output
      if output is None:
         stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
         output = Path("g17p-cts-artifacts") / stamp
      output = output.resolve()
      if output.exists() and any(output.iterdir()):
         raise BringupError(f"output directory is not empty: {output}")
      output.mkdir(parents=True, exist_ok=True)

      state: dict[str, Any] = {
         "schema": 1,
         "target": config.get("target"),
         "started_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
         "dry_run": args.dry_run,
         "status": "planned" if args.dry_run else "running",
         "completed_stages": [],
         "commands": [],
         "invocation": {
            "argv": [sys.executable, str(Path(__file__).resolve()), *raw_argv],
            "cwd": str(Path.cwd().resolve()),
         },
         "inputs": [
            input_record(args.config, "stage-config"),
            input_record(args.mustpass, "vulkan-cts-mustpass"),
         ],
      }
      update_manifest(output, state)

      selected_stages = []
      for stage in config["stages"]:
         selected_stages.append(stage)
         if stage["name"] == args.through:
            break
      patterns = [
         selector["regex"]
         for stage in selected_stages if not stage.get("rerun_prior")
         for selector in stage["selectors"]
      ]
      candidate_cases, total_cases = scan_mustpass(args.mustpass, patterns)
      plan = build_plan(config, candidate_cases, total_cases, args.mustpass,
                        args.through, args.allow_full_cts, output, args.deqp_vk)
      plan.update({
         "config": str(args.config.resolve()),
         "mustpass": str(args.mustpass.resolve()),
         "deqp_vk": args.deqp_vk,
         "deqp_runner": args.deqp_runner,
         "vulkaninfo": args.vulkaninfo,
         "render_node": str(args.render_node),
         "jobs": args.jobs,
      })
      write_json(output / "plan.json", plan)
      update_manifest(output, state)
      if args.dry_run:
         print(f"dry-run plan: {output / 'plan.json'}")
         for stage in plan["stages"]:
            print(f"  {stage['number']:02d} {stage['name']}: "
                  f"{stage['case_count']} cases, {stage['timeout_seconds']}s timeout")
         print("no device, DRM, Vulkan, dmesg, or CTS command was executed")
         return 0

      for name, command in (("deqp-runner", args.deqp_runner),
                            ("deqp-vk", args.deqp_vk),
                            ("vulkaninfo", args.vulkaninfo),
                            ("dmesg", "dmesg")):
         if command_path(command) is None:
            raise BringupError(f"preflight: {name} executable not found: {command}")

      state["host_commands"] = capture_host(output, args.render_node)
      state["drm"] = inspect_drm(args.render_node, config["drm_driver_regex"])
      update_manifest(output, state)

      kernel_dir = output / "kernel"
      kernel_dir.mkdir(exist_ok=True)
      baseline_result = dmesg_snapshot(kernel_dir / "00-before-device.log")
      state["commands"].append(baseline_result)
      if baseline_result["exit_code"] != 0 and not args.allow_unreadable_dmesg:
         raise BringupError("kernel gate: dmesg is unreadable (use explicit "
                            "--allow-unreadable-dmesg only if external capture exists)")
      baseline = (dmesg_payload(kernel_dir / "00-before-device.log")
                  if baseline_result["exit_code"] == 0 else [])
      require_dmesg = baseline_result["exit_code"] == 0
      if require_dmesg:
         monitor = KernelLogMonitor(kernel_dir / "continuous.log")
         state["kernel_monitor"] = monitor.start()
         kernel_monitor = monitor

      device_dir = output / "device"
      summary_result = run_logged([args.vulkaninfo, "--summary"],
                                  device_dir / "vulkaninfo-summary.log", 60)
      state["commands"].append(summary_result)
      after_device_result = dmesg_snapshot(kernel_dir / "01-after-device.log")
      state["commands"].append(after_device_result)
      if require_dmesg and after_device_result["exit_code"] != 0:
         raise BringupError("device gate: lost dmesg access after Vulkan probe")
      device_faults: list[str] = []
      if after_device_result["exit_code"] == 0:
         after_device = dmesg_payload(kernel_dir / "01-after-device.log")
         new_lines = new_dmesg_lines(baseline, after_device)
         if kernel_monitor is not None:
            new_lines.extend(kernel_monitor.read_new_lines())
            kernel_monitor.require_alive("device gate")
         device_faults = matching_faults(
            dict.fromkeys(new_lines), config["kernel_fault_regex"]
         )
         if device_faults:
            (kernel_dir / "01-device-faults.log").write_text(
               "\n".join(device_faults) + "\n", encoding="utf-8")
         baseline = after_device
      if summary_result["exit_code"] != 0 or summary_result["timed_out"]:
         raise BringupError("device gate: vulkaninfo --summary failed")
      if device_faults:
         raise BringupError("device gate: new kernel fault signatures detected")
      summary = (device_dir / "vulkaninfo-summary.log").read_text(
         encoding="utf-8", errors="replace")
      if not re.search(config["device_regex"], summary):
         raise BringupError("device gate: vulkaninfo does not identify A18 Pro/G17P")
      if not re.search(config["driver_regex"], summary):
         raise BringupError("device gate: vulkaninfo does not identify Honeykrisp")

      json_result = run_logged([args.vulkaninfo, "--json"],
                               device_dir / "vulkaninfo.json.log", 60)
      state["commands"].append(json_result)
      after_properties_result = dmesg_snapshot(kernel_dir / "02-after-properties.log")
      state["commands"].append(after_properties_result)
      if require_dmesg and after_properties_result["exit_code"] != 0:
         raise BringupError("device properties gate: lost dmesg access")
      property_faults: list[str] = []
      if after_properties_result["exit_code"] == 0:
         after_properties = dmesg_payload(kernel_dir / "02-after-properties.log")
         new_lines = new_dmesg_lines(baseline, after_properties)
         if kernel_monitor is not None:
            new_lines.extend(kernel_monitor.read_new_lines())
            kernel_monitor.require_alive("device properties gate")
         property_faults = matching_faults(
            dict.fromkeys(new_lines), config["kernel_fault_regex"],
         )
         if property_faults:
            (kernel_dir / "02-properties-faults.log").write_text(
               "\n".join(property_faults) + "\n", encoding="utf-8")
         baseline = after_properties
      if json_result["exit_code"] != 0 or json_result["timed_out"]:
         raise BringupError("device gate: vulkaninfo --json failed")
      if property_faults:
         raise BringupError("device properties gate: new kernel fault signatures detected")
      update_manifest(output, state)

      for stage in plan["stages"]:
         print(f"running {stage['name']} gate ({stage['case_count']} cases)",
               flush=True)
         stage_dir = Path(stage["directory"])
         results_dir = stage_dir / "results"
         command = [
            args.deqp_runner, "suite",
            "--suite", str(stage_dir / "suite.toml"),
            "--output", str(results_dir),
            "--jobs", str(args.jobs),
            "--max-fails", "1",
         ]
         result = run_logged(command, stage_dir / "runner.log",
                             stage["timeout_seconds"])
         state["commands"].append(result)

         kernel_result = dmesg_snapshot(
            kernel_dir / f"{stage['number'] + 2:02d}-after-{stage['name']}.log")
         state["commands"].append(kernel_result)
         if require_dmesg and kernel_result["exit_code"] != 0:
            raise BringupError(f"{stage['name']} gate: lost dmesg access")
         faults: list[str] = []
         if kernel_result["exit_code"] == 0:
            after = dmesg_payload(
               kernel_dir / f"{stage['number'] + 2:02d}-after-{stage['name']}.log")
            new_lines = new_dmesg_lines(baseline, after)
            if kernel_monitor is not None:
               new_lines.extend(kernel_monitor.read_new_lines())
               kernel_monitor.require_alive(f"{stage['name']} gate")
            faults = matching_faults(
               dict.fromkeys(new_lines), config["kernel_fault_regex"]
            )
            baseline = after
         if faults:
            (stage_dir / "kernel-faults.log").write_text(
               "\n".join(faults) + "\n", encoding="utf-8")

         if result["timed_out"]:
            raise BringupError(f"{stage['name']} gate timed out")
         if result["exit_code"] != 0:
            raise BringupError(f"{stage['name']} gate failed with exit code "
                               f"{result['exit_code']}")
         if faults:
            raise BringupError(f"{stage['name']} gate produced kernel faults")
         if stage["name"] != "full":
            proof = validate_proof_stage_results(stage_dir, stage["case_count"])
            state.setdefault("proof_results", {})[stage["name"]] = proof
         state["completed_stages"].append(stage["name"])
         if stage["number"] + 1 < len(plan["stages"]):
            update_manifest(output, state)

      if kernel_monitor is not None:
         monitor_result, monitor_tail = kernel_monitor.stop()
         kernel_monitor = None
         state["kernel_monitor"] = monitor_result
         tail_faults = matching_faults(
            monitor_tail, config["kernel_fault_regex"]
         )
         if tail_faults:
            (kernel_dir / "continuous-tail-faults.log").write_text(
               "\n".join(tail_faults) + "\n", encoding="utf-8"
            )
         if monitor_result["premature_exit"]:
            raise BringupError("kernel gate: continuous kernel monitor exited early")
         if tail_faults:
            raise BringupError("kernel gate: trailing kernel fault signatures detected")

      state["status"] = "passed"
      state["finished_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
      summary = {
         "schema": 1,
         "target": config.get("target"),
         "status": "passed",
         "through": args.through,
         "completed_stages": state["completed_stages"],
         "artifact_manifest": str((output / "artifact-manifest.json").resolve()),
      }
      write_json(output / "summary.json", summary)
      update_manifest(output, state)
      print(f"all requested gates passed; artifacts: {output}")
      print(success_marker(args.through))
      return 0
   except (BringupError, OSError) as exc:
      print(f"g17p staged CTS: {exc}", file=sys.stderr)
      if kernel_monitor is not None:
         try:
            monitor_result, monitor_tail = kernel_monitor.stop()
            kernel_monitor = None
            if "state" in locals():
               state["kernel_monitor"] = monitor_result
            tail_faults = matching_faults(
               monitor_tail, config.get("kernel_fault_regex", r"(?!)")
            )
            if tail_faults and "kernel_dir" in locals():
               (kernel_dir / "continuous-tail-faults.log").write_text(
                  "\n".join(tail_faults) + "\n", encoding="utf-8"
               )
         except (BringupError, OSError, subprocess.SubprocessError) as monitor_exc:
            print(f"g17p staged CTS: kernel monitor cleanup: {monitor_exc}",
                  file=sys.stderr)
      if "output" in locals() and output.exists():
         state = locals().get("state", {
            "schema": 1, "target": "Apple A18 Pro / G17P",
            "dry_run": args.dry_run, "completed_stages": [], "commands": [],
         })
         state["status"] = "failed"
         state["failure"] = str(exc)
         state["finished_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
         write_json(output / "summary.json", {
            "schema": 1,
            "target": state.get("target", "Apple A18 Pro / G17P"),
            "status": "failed",
            "through": args.through,
            "completed_stages": state.get("completed_stages", []),
            "failure": str(exc),
         })
         update_manifest(output, state)
      return 2


if __name__ == "__main__":
   raise SystemExit(main())
