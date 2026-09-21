"""Evidence-based CTS memory isolation; never changes terminal test outcomes."""
import json
from pathlib import Path
import re


def active_qpa_cases(directory):
    """Best-effort names in unfinished QPA tails, not a culprit attribution."""
    active = set()
    for path in Path(directory).glob('*.qpa'):
        if not re.search(r'(?:^|\.)c\d+\.r\d+\.qpa$', path.name):
            continue
        try:
            with path.open('rb') as f:
                f.seek(max(0, path.stat().st_size - 65536))
                tail = f.read().decode(errors='replace')
        except OSError:
            continue
        starts = list(re.finditer(r'^#beginTestCaseResult (.+)$', tail, re.M))
        if starts and '#endTestCaseResult' not in tail[starts[-1].end():]:
            active.add(starts[-1].group(1).strip())
    return sorted(active)


def classify_memory_stop(summary, evidence, rows, current_boot):
    """Only a proven, otherwise healthy parallel memory stop can auto-resume.

    Actual OOM or terminal crashes are recorded as candidates but still require
    explicit review; they must never become an invisible automatic retry.
    """
    try:
        remote = Path(evidence) / 'remote'
        guard = json.loads((remote / 'result.json').read_text())
        timing = json.loads((remote / 'cts/timing.json').read_text())
        runner = json.loads((remote / 'cts/runner.json').read_text())
        samples = [json.loads(s) for s in (remote / 'cts/memory.jsonl').read_text().splitlines()]
        before = json.loads((Path(evidence) / 'target-before.json').read_text())
        report = json.loads((Path(evidence) / 'report.json').read_text())
        thermal = guard['thermal']
        if (not report['evidence_fetched'] or report['verdict'] not in ('pass', 'process_failure') or
                not current_boot or before['boot_id'] != current_boot or
                guard['boot_id'] != current_boot or thermal['failure'] is not None or
                thermal['samples'] < 1 or summary['peak_temperature_c'] >= 100 or
                summary['guard_verdict'] not in ('pass', 'process_failure') or
                not guard['watchdog']['armed'] or guard['watchdog']['timeout_seconds'] != 60):
            return None
        available = [s['meminfo_kib']['MemAvailable'] for s in samples]
        kills = [s['vmstat']['oom_kill'] for s in samples]
        if not available or any(v < 0 for v in available) or any(k < kills[0] for k in kills):
            return None
        oom = max(kills) > kills[0]
        floor = runner['memory_floor_mib']
        floor_stop = ('CTS memory floor reached: available_kib=' in (timing['error'] or '')
                      and min(available) < floor * 1024)
        if not floor_stop and not oom:
            return None
        captured = set()
        pressure = remote / 'cts/memory-pressure.json'
        if pressure.exists():
            captured.update(json.loads(pressure.read_text())['active_cases'])
        candidates = [r for r in rows if r['status'] == 'Interrupted' or
                      r['case'] in captured or (oom and r['status'] == 'Crash')]
        if not candidates:
            return None  # Cannot choose a case safely from batch pressure alone.
        fatal = any(r['status'] in ('Crash', 'Timeout', 'HarnessError') for r in rows)
        return dict(label='RAM-heavy candidate', actual_oom=oom,
                    minimum_available_kib=min(available), floor_mib=floor,
                    original_jobs=timing['jobs'],
                    can_resume=(floor_stop and not oom and not fatal and timing['jobs'] > 1),
                    candidates=[dict(index=r['eligibility']['catalog_index'], case=r['case'],
                                     original_status=r['status']) for r in candidates],
                    attribution='Concurrent activity during memory pressure; not a proven per-case RAM requirement')
    except (OSError, ValueError, KeyError, TypeError):
        return None  # Missing/corrupt evidence cannot authorize continuation.


def isolate_selection(start, end, done, serial_indices):
    """Group adjacent flagged cases for sequential fresh processes, or exclude them from a pool."""
    if done[start]:
        raise ValueError('A terminal case cannot be scheduled again')
    if start in serial_indices:
        selected = []
        for i in range(start, end):
            if done[i]:
                continue
            if i not in serial_indices:
                break
            selected.append(i)
        return selected, True
    return [i for i in range(start, end) if not done[i] and i not in serial_indices], False
