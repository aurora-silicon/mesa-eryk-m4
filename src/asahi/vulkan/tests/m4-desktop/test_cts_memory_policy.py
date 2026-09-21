#!/usr/bin/env python3
"""Offline safety and coverage tests for adaptive CTS memory isolation."""
import json
from pathlib import Path
import tempfile
import unittest
from cts_memory_policy import active_qpa_cases, classify_memory_stop, isolate_selection


class MemoryPolicyTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        (self.path/'remote/cts').mkdir(parents=True)
        self.put('target-before.json', {'boot_id': 'same'})
        self.put('report.json', dict(evidence_fetched=True, verdict='process_failure'))
        self.guard = dict(boot_id='same', thermal=dict(failure=None, samples=4),
                          watchdog=dict(armed=True, timeout_seconds=60))
        self.put('remote/result.json', self.guard)
        self.timing = dict(error='RuntimeError(CTS memory floor reached: available_kib=1500000)', jobs=3)
        self.put('remote/cts/timing.json', self.timing)
        self.put('remote/cts/runner.json', dict(memory_floor_mib=1536))
        self.samples = [dict(meminfo_kib=dict(MemAvailable=v), vmstat=dict(oom_kill=0))
                        for v in (6000000, 1500000)]
        self.memory()
        self.summary = dict(peak_temperature_c=80, guard_verdict='process_failure')
        self.rows = [dict(case='test.a', status='Pass', eligibility=dict(catalog_index=1)),
                     dict(case='test.b', status='Interrupted', eligibility=dict(catalog_index=2)),
                     dict(case='test.c', status='Unrun', eligibility=dict(catalog_index=3))]

    def put(self, path, value):
        (self.path/path).write_text(json.dumps(value))

    def memory(self):
        (self.path/'remote/cts/memory.jsonl').write_text(''.join(json.dumps(s)+'\n' for s in self.samples))

    def classify(self, boot='same'):
        return classify_memory_stop(self.summary, self.path, self.rows, boot)

    def test_positive_floor_stop_isolates_only_candidates(self):
        result = self.classify()
        self.assertTrue(result['can_resume'])
        self.assertFalse(result['actual_oom'])
        self.assertEqual([r['index'] for r in result['candidates']], [2])
        self.assertEqual(self.rows[0]['status'], 'Pass')

    def test_thermal_or_watchdog_failure_never_authorizes_resume(self):
        for change in ('thermal', 'watchdog'):
            guard = json.loads(json.dumps(self.guard))
            if change == 'thermal':
                guard['thermal']['failure'] = 'sensor stale'
            else:
                guard['watchdog']['armed'] = False
            self.put('remote/result.json', guard)
            self.assertIsNone(self.classify())

    def test_fault_verdict_changed_boot_missing_or_partial_evidence_rejected(self):
        self.assertIsNone(self.classify('different'))
        self.summary['guard_verdict'] = 'kernel_fault'
        self.assertIsNone(self.classify())
        self.summary['guard_verdict'] = 'process_failure'
        self.put('report.json', dict(evidence_fetched=False, verdict='process_failure'))
        self.assertIsNone(self.classify())
        (self.path/'remote/cts/timing.json').unlink()
        self.assertIsNone(self.classify())

    def test_actual_oom_is_marked_but_requires_review(self):
        self.samples[-1]['vmstat']['oom_kill'] = 1
        self.memory()
        self.rows[1]['status'] = 'Crash'
        result = self.classify()
        self.assertTrue(result['actual_oom'])
        self.assertFalse(result['can_resume'])
        self.assertEqual(self.rows[1]['status'], 'Crash')

    def test_timeout_or_crash_is_never_masked(self):
        for status in ('Crash', 'Timeout', 'HarnessError'):
            self.rows[0]['status'] = status
            self.assertFalse(self.classify()['can_resume'])

    def test_single_worker_pressure_stops_instead_of_retry_loop(self):
        self.timing['jobs'] = 1
        self.put('remote/cts/timing.json', self.timing)
        self.assertFalse(self.classify()['can_resume'])

    def test_unproven_pressure_or_missing_candidate_does_not_authorize(self):
        self.samples[-1]['meminfo_kib']['MemAvailable'] = 3000000
        self.memory()
        self.assertIsNone(self.classify())
        self.samples[-1]['meminfo_kib']['MemAvailable'] = 1500000
        self.memory()
        self.rows[1]['status'] = 'Unrun'
        self.assertIsNone(self.classify())

    def test_active_tail_does_not_reclassify_finished_outcomes(self):
        self.put('remote/cts/memory-pressure.json', dict(active_cases=['test.a', 'test.b', 'unknown']))
        result = self.classify()
        self.assertEqual([r['index'] for r in result['candidates']], [1, 2])
        self.assertEqual(self.rows[0]['status'], 'Pass')

    def test_qpa_active_tails_ignore_completed_and_extracted_logs(self):
        (self.path/'c0.r0.qpa').write_text('#beginTestCaseResult test.a\nbody\n#endTestCaseResult\n')
        (self.path/'c1.r0.qpa').write_text('#beginTestCaseResult test.b\npartial')
        (self.path/'failure.qpa').write_text('#beginTestCaseResult fake\n')
        self.assertEqual(active_qpa_cases(self.path), ['test.b'])

    def test_scheduler_preserves_indices_and_never_repeats_terminal_cases(self):
        done = bytearray([1, 0, 0, 0, 1, 0])
        selected, isolated = isolate_selection(1, 6, done, {2, 4})
        self.assertEqual(selected, [1, 3, 5])
        self.assertFalse(isolated)
        self.assertEqual(isolate_selection(2, 6, done, {2, 4}), ([2], True))
        with self.assertRaises(ValueError):
            isolate_selection(4, 6, done, {2, 4})

    def test_adjacent_candidates_share_guard_but_not_worker_process(self):
        done = bytearray([1, 0, 0, 1, 0, 0])
        self.assertEqual(isolate_selection(1, 6, done, {1, 2, 4}), ([1, 2, 4], True))
        self.assertEqual(isolate_selection(1, 3, done, {1, 2, 4}), ([1, 2], True))


if __name__ == '__main__':
    unittest.main()
