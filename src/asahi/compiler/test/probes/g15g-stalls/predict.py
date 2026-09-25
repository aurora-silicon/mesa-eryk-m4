#!/usr/bin/env python3
"""Reference model of Apple's G15G stall placement; compares it with Apple.

Input: compiled corpus JSON (compile.py). For each shader the G15G stalls are
stripped, the rule is re-applied to the remaining stream, and the predicted
(position, value) pairs are compared with Apple's.

Rule (mirrors src/asahi/compiler/agx3_g15g_stalls.c):
  * producers: an instruction with signal slot f12 in 1..6 and destination
    file f9 == 2 (general registers) makes that slot "GPR-pending";
  * a may-dataflow over the CFG (b/bn edges, fallthrough except after end,
    plus an edge from each cndst/cndef/cndlt/cndcf to the else/pop closing
    its exec-mask region, since a region with no active lanes is skipped)
    tracks the pending slots; an instruction's wait mask clears its slots;
  * an instruction whose wait mask W (end: all six slots) meets the pending
    set P gets a stall `06 ((W & P) << 2)`, `06 00` x3 in front of it.
Environment: CONSERVATIVE=0 treats b as unconditional and opens regions only
at cndst/cndef (same result on the corpus); REGION_SKIP=0, END_DRAIN=0 and
ALL_WAITS=1 switch off parts of the rule to show what each one is needed for.
"""
import json, sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stalls import instrs, find_stalls
from fields import decompose_many
BASE = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wait-base.json')))
BRANCH = {'b', 'bn'}
REGION_SKIP = os.environ.get('REGION_SKIP', '1') == '1'
UNCOND = set() if os.environ.get('CONSERVATIVE', '1') == '1' else {'b'}

def analyse(s, dec):
    n = len(s)
    info = []
    offs = {x['off']: k for k, x in enumerate(s)}
    for k, x in enumerate(s):
        m = dec[x['hex']][0]; mn = m['mnemonic']; fl = m['fields']
        if mn not in BASE and mn not in ('end',):
            raise KeyError(mn)
        w = sum(fl.get(BASE[mn] + j, 0) << j for j in range(6)) if mn in BASE else 0
        slot = fl.get(12, 0) if fl.get(9, 0) in (2, 4, 5) else 0
        gpr = fl.get(9, 0) == 2
        succ = []
        if mn in BRANCH:
            disp = fl.get(0, 0)
            if disp >= 1 << 47: disp -= 1 << 48
            succ.append(offs[x['off'] + disp])
        if mn not in UNCOND and not (mn == 'end' and k + 1 < n and s[k + 1]['name'] == 'nop_ep0'):
            if k + 1 < n: succ.append(k + 1)
        if mn == 'end' and os.environ.get('END_DRAIN', '1') == '1': w = 0x3f  # program end drains every GPR slot
        info.append(dict(mn=mn, w=w, slot=slot, gpr=gpr, succ=succ, fl=fl))
    # predicated regions may be skipped entirely: cndst/cndef -> the cndef/cndend closing it
    if REGION_SKIP:
        # exec-mask stack depth before each instruction: cndst pushes f12
        # levels, cndend pops f10 levels (cndef/cndlt/cndcf keep the depth)
        OPEN = ('cndst', 'cndef') + (('cndlt', 'cndcf') if os.environ.get('CONSERVATIVE', '1') == '1' else ())
        for k, it in enumerate(info):
            if it['mn'] not in OPEN: continue
            depth = 0
            for j in range(k + 1, n):
                mj = info[j]['mn']
                if mj == 'cndst': depth += info[j]['fl'].get(12, 1)
                elif mj == 'cndef' and depth == 0 and it['mn'] == 'cndst': it['succ'].append(j); break
                elif mj == 'cndend':
                    depth -= info[j]['fl'].get(10, 1)
                    if depth < 0: it['succ'].append(j); break
    # forward dataflow: IN[k] = bitmask of slots with a possibly-outstanding GPR producer
    IN = [0] * n; IN_seen = [False] * n; IN_seen[0] = True
    # every program in the container (constant program, main) is an entry
    for k in range(1, n):
        if info[k]['mn'] != 'nop' and info[k - 1]['mn'] in ('end', 'nop') and s[k]['off'] % 2 == 0:
            j = k - 1
            while j >= 0 and info[j]['mn'] == 'nop': j -= 1
            if j >= 0 and info[j]['mn'] == 'end': IN_seen[k] = True
    changed = True
    while changed:
        changed = False
        for k in range(n):
            if not IN_seen[k]: continue
            st = IN[k] & ~info[k]['w']
            if info[k]['slot'] and 1 <= info[k]['slot'] <= 6:
                bit = 1 << (info[k]['slot'] - 1)
                st = (st | bit) if info[k]['gpr'] else st   # non-GPR producer: does not add GPR pending
            for t in info[k]['succ']:
                nv = IN[t] | st
                if nv != IN[t] or not IN_seen[t]:
                    IN[t] = nv; IN_seen[t] = True; changed = True
    if os.environ.get('ALL_WAITS') == '1':
        return [(k, info[k]['w']) for k in range(n) if info[k]['w']], info
    return [(k, info[k]['w'] & IN[k]) for k in range(n) if info[k]['w'] and (IN[k] & info[k]['w'])], info

if __name__ == '__main__':
    tot = collections.Counter(); bad = []
    for f in sys.argv[1:]:
        for name, d in json.load(open(f))['kernels'].items():
            st, s = find_stalls(instrs(d['applegpu_g15g']))
            dec = decompose_many([x['hex'] for x in s])
            try:
                pred, info = analyse(s, dec)
            except KeyError as e:
                tot['unknown_mnemonic'] += 1; print('UNKNOWN', name, e); continue
            actual = [(x['index'], x['value']) for x in st]
            tot['kernels'] += 1; tot['stalls'] += len(actual)
            if pred == actual: tot['kernel_exact'] += 1
            else:
                bad.append((name, sorted(set(pred) - set(actual)), sorted(set(actual) - set(pred)), [info[k]['mn'] for k, _ in set(pred) ^ set(actual)]))
            tot['stall_tp'] += len(set(pred) & set(actual)); tot['stall_fp'] += len(set(pred) - set(actual)); tot['stall_fn'] += len(set(actual) - set(pred))
    print(dict(tot))
    for b in bad: print('MISMATCH', b)
