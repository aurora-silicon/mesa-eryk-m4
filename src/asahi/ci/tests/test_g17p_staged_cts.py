#!/usr/bin/env python3

import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest


CI_DIR = Path(__file__).resolve().parents[1]
SCRIPT = CI_DIR / "run_g17p_staged_cts.py"
CONFIG = CI_DIR / "g17p-cts-stages.json"
SPEC = importlib.util.spec_from_file_location("g17p_staged_cts", SCRIPT)
RUNNER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(RUNNER)


def fixture_cases() -> list[str]:
   cases = [f"dEQP-VK.info.case_{number:03d}" for number in range(19)]
   cases += [f"dEQP-VK.api.info.format.case_{number:03d}" for number in range(64)]
   cases += [
      "dEQP-VK.compute.pipeline.basic.copy_ssbo_single_invocation",
      "dEQP-VK.compute.pipeline.basic.empty_shader",
      "dEQP-VK.compute.pipeline.basic.shared_var_single_invocation",
      "dEQP-VK.compute.pipeline.basic.ubo_to_ssbo_single_invocation",
   ]
   cases += [
      f"dEQP-VK.compute.pipeline.basic.{name}"
      for name in (
         "copy_image_to_ssbo_large", "copy_image_to_ssbo_small",
         "copy_ssbo_multiple_groups", "copy_ssbo_multiple_invocations",
         "copy_ssbo_to_image_large", "copy_ssbo_to_image_small",
         "empty_workgroup_all", "empty_workgroup_x", "empty_workgroup_y",
         "empty_workgroup_z", "ssbo_rw_multiple_groups",
         "ssbo_rw_single_invocation", "ubo_to_ssbo_multiple_groups",
         "ubo_to_ssbo_multiple_invocations", "ubo_to_ssbo_single_group",
      )
   ]
   cases += [
      f"dEQP-VK.draw.renderpass.basic_draw.{command}.triangle_{primitive}.{count}"
      for command in ("draw", "draw_indexed")
      for primitive in ("fan", "list", "strip")
      for count in ("1", "3", "17", "45")
   ]
   cases += [f"dEQP-VK.memory.case_{number:03d}" for number in range(4)]
   return cases


class G17PStagedCTSTests(unittest.TestCase):
   def run_script(self, *args: str, env=None) -> subprocess.CompletedProcess[str]:
      return subprocess.run(
         [sys.executable, str(SCRIPT), *args], text=True,
         stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
         check=False,
      )

   def test_config_and_selection_keep_gates_ordered_and_disjoint(self):
      config = RUNNER.read_json(CONFIG)
      RUNNER.validate_config(config)
      prior = set()
      counts = {}
      for stage in config["stages"][:-1]:
         cases, _ = RUNNER.select_cases(fixture_cases(), stage, prior)
         self.assertTrue(prior.isdisjoint(cases))
         prior.update(cases)
         counts[stage["name"]] = len(cases)
      self.assertEqual(counts, {
         "loader": 12, "compute": 4, "triangle": 4, "expanded": 49,
      })

   def test_dry_run_needs_no_gpu_and_writes_reproducible_plan(self):
      with tempfile.TemporaryDirectory() as temporary:
         root = Path(temporary)
         mustpass = root / "vk-main.txt"
         mustpass.write_text("\n".join(reversed(fixture_cases())) + "\n")
         output = root / "artifacts"
         result = self.run_script(
            "--dry-run", "--config", str(CONFIG), "--mustpass", str(mustpass),
            "--output", str(output), "--deqp-vk", "/missing/deqp-vk",
            "--deqp-runner", "/missing/deqp-runner",
            "--vulkaninfo", "/missing/vulkaninfo", "--through", "triangle",
         )
         self.assertEqual(result.returncode, 0, result.stderr)
         self.assertIn("no device, DRM, Vulkan, dmesg, or CTS command", result.stdout)
         plan = json.loads((output / "plan.json").read_text())
         self.assertEqual([stage["name"] for stage in plan["stages"]],
                          ["loader", "compute", "triangle"])
         self.assertEqual([stage["case_count"] for stage in plan["stages"]],
                          [12, 4, 4])
         first = (output / "00-loader/caselist.txt").read_text().splitlines()
         self.assertEqual(first, sorted(first))
         manifest = json.loads((output / "artifact-manifest.json").read_text())
         self.assertEqual(manifest["status"], "planned")
         self.assertEqual(manifest["invocation"]["cwd"], str(Path.cwd()))
         self.assertEqual([item["role"] for item in manifest["inputs"]],
                          ["stage-config", "vulkan-cts-mustpass"])
         self.assertTrue(any(item["path"] == "plan.json"
                             for item in manifest["artifacts"]))

   def test_full_cts_requires_second_explicit_switch(self):
      with tempfile.TemporaryDirectory() as temporary:
         root = Path(temporary)
         mustpass = root / "vk-main.txt"
         mustpass.write_text("\n".join(fixture_cases()) + "\n")
         result = self.run_script(
            "--dry-run", "--config", str(CONFIG), "--mustpass", str(mustpass),
            "--output", str(root / "artifacts"), "--through", "full",
         )
         self.assertEqual(result.returncode, 2)
         self.assertIn("requires --allow-full-cts", result.stderr)

   def test_full_cts_reuses_authoritative_list_without_expanding_it(self):
      with tempfile.TemporaryDirectory() as temporary:
         root = Path(temporary)
         mustpass = root / "vk-main.txt"
         mustpass.write_text("\n".join(fixture_cases()) + "\n")
         output = root / "artifacts"
         result = self.run_script(
            "--dry-run", "--config", str(CONFIG), "--mustpass", str(mustpass),
            "--output", str(output), "--through", "full", "--allow-full-cts",
         )
         self.assertEqual(result.returncode, 0, result.stderr)
         full = output / "04-full"
         self.assertFalse((full / "caselist.txt").exists())
         source = json.loads((full / "caselist-source.json").read_text())
         self.assertEqual(source["path"], str(mustpass.resolve()))
         suite = (full / "suite.toml").read_text()
         self.assertIn(str(mustpass.resolve()), suite)
         self.assertIn("tests_per_group = 1000", suite)

   def test_ladder_artifact_environment_is_the_output_root(self):
      with tempfile.TemporaryDirectory() as temporary:
         root = Path(temporary)
         mustpass = root / "vk-main.txt"
         mustpass.write_text("\n".join(fixture_cases()) + "\n")
         output = root / "ladder-artifacts"
         env = os.environ.copy()
         env["A18_GPU_ARTIFACT_DIR"] = str(output)
         result = self.run_script(
            "--dry-run", "--config", str(CONFIG), "--mustpass", str(mustpass),
            env=env,
         )
         self.assertEqual(result.returncode, 0, result.stderr)
         self.assertTrue((output / "artifact-manifest.json").is_file())

   def test_capability_overrides_are_rejected_even_in_dry_run(self):
      with tempfile.TemporaryDirectory() as temporary:
         root = Path(temporary)
         mustpass = root / "vk-main.txt"
         mustpass.write_text("\n".join(fixture_cases()) + "\n")
         env = os.environ.copy()
         env["MESA_VK_VERSION_OVERRIDE"] = "1.3"
         result = self.run_script(
            "--dry-run", "--config", str(CONFIG), "--mustpass", str(mustpass),
            "--output", str(root / "artifacts"), env=env,
         )
         self.assertEqual(result.returncode, 2)
         self.assertIn("refusing capability overrides", result.stderr)

   def test_dmesg_wrap_fails_safe_by_scanning_whole_snapshot(self):
      before = ["old line", "another old line"]
      after = ["GPU fault: wrapped ring"]
      self.assertEqual(RUNNER.new_dmesg_lines(before, after), after)

   def test_kernel_fault_matcher_catches_gpu_hang_and_iommu_fault(self):
      config = RUNNER.read_json(CONFIG)
      faults = RUNNER.matching_faults(
         ["asahi: GPU timeout waiting for queue", "IOMMU fault at 0x1234",
          "asahi: initialized normally"],
         config["kernel_fault_regex"],
      )
      self.assertEqual(len(faults), 2)

   def test_drm_gate_rejects_a_regular_file(self):
      with tempfile.TemporaryDirectory() as temporary:
         node = Path(temporary) / "renderD128"
         node.write_text("not a device")
         self.assertTrue(stat.S_ISREG(node.stat().st_mode))
         with self.assertRaises(RUNNER.BringupError):
            RUNNER.inspect_drm(node, "^asahi$")

   def test_logged_command_timeout_is_fail_closed(self):
      with tempfile.TemporaryDirectory() as temporary:
         result = RUNNER.run_logged(
            [sys.executable, "-c", "import time; time.sleep(10)"],
            Path(temporary) / "timeout.log", 1,
         )
         self.assertTrue(result["timed_out"])
         self.assertNotEqual(result["exit_code"], 0)

   def test_terminal_markers_distinguish_smoke_from_full(self):
      self.assertEqual(RUNNER.success_marker("compute"),
                       "A18_GPU_COMPUTE_PASS")
      self.assertEqual(RUNNER.success_marker("triangle"),
                       "A18_GPU_TRIANGLE_PASS")
      self.assertEqual(RUNNER.success_marker("expanded"),
                       "A18_GPU_CTS_SMOKE_PASS")
      self.assertEqual(RUNNER.success_marker("full"),
                       "A18_GPU_CTS_FULL_PASS")


if __name__ == "__main__":
   unittest.main()
