#!/usr/bin/env python3
"""Exercise the actual shared timing snapshot with concurrent readers."""
from pathlib import Path
import subprocess
import tempfile

paths = [Path('linux') / repo / 'drivers/gpu/drm/asahi/agx_timing_stats.rs'
         for repo in ('linux-asahi', 'linux-asahi-m3', 'm3-image64-publish')]
source = paths[0].read_text()
assert all(path.read_text() == source for path in paths)
body = source[source.index('pub(crate) struct Stats'):source.index('    pub(crate) fn write(')] + '}\n'
harness = r'''
use std::sync::{Arc, atomic::{fence, AtomicU64, AtomicBool, Ordering}};
/*BODY*/
fn check(v: [u64; 18]) {
    for row in v.chunks_exact(9) {
        let n = row[0];
        assert_eq!(row, &[n, 3*n, n, 2*n, 5*n, 7*n, 11*n, 13*n, 17*n]);
    }
}
fn main() {
    let s = Stats::new();
    assert_eq!(s.snapshot(), Some([0; 18]));
    s.sequence.store(1, Ordering::Release);
    assert_eq!(s.snapshot(), None, "busy writer must not block readers");
    s.sequence.store(0, Ordering::Release);
    s.record(true, 4, -1, -8, 0, [0; 3]);
    assert_eq!(&s.snapshot().unwrap()[..9], &[1,4,0,0,0,0,0,0,0]);
    let s = Arc::new(Stats::new());
    let done = Arc::new(AtomicBool::new(false));
    let mut readers = Vec::new();
    for _ in 0..4 {
        let s = s.clone(); let done = done.clone();
        readers.push(std::thread::spawn(move || {
            while !done.load(Ordering::Acquire) {
                if let Some(v) = s.snapshot() { check(v); }
            }
            check(s.snapshot().unwrap());
        }));
    }
    for _ in 0..100000 {
        s.record(true, 3, 2, 5, 7, [11,13,17]);
        s.record(false, 3, 2, 5, 7, [11,13,17]);
    }
    done.store(true, Ordering::Release);
    for reader in readers { reader.join().unwrap(); }
    check(s.snapshot().unwrap());
    assert_eq!(s.snapshot().unwrap()[0], 100000);
    assert_eq!(s.snapshot().unwrap()[9], 100000);
    println!("PASS coherent render/compute snapshots, batch command accounting, negative CPU clamp, bounded busy read");
}
'''.replace('/*BODY*/', body)
with tempfile.TemporaryDirectory(prefix='asahi-timing-') as directory:
    src = Path(directory) / 'test.rs'
    src.write_text(harness)
    exe = src.with_suffix('')
    subprocess.run(['rustc', '--edition=2021', '-O', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
