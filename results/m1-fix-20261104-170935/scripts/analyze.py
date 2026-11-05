#!/usr/bin/env python3
"""Parse raw/meas/*.log and raw/csv/*.log -> summary CSVs + markdown tables (stdout)."""
import csv, os, re, statistics, sys

R = sys.argv[1]
CFG = [("baseline", "Baseline"), ("barrettmul", "Barrett Mul"), ("montlazy", "Mont.+Lazy NTT"), ("integrated", "Integrated")]
MODES = [("sym", "SYM"), ("asym", "ASYM")]
NS = [1024, 2048, 4096]
NP = {1024: 1, 2048: 1, 4096: 3}
# IEEE Access draft (2026_IEEE_Access_CKKS_ARMv7_8 (1).pdf), Table 5: Apple M1
PAPER = {
    ("baseline", "sym"): [116898, 234478, 477361], ("baseline", "asym"): [108229, 227205, 477284],
    ("barrettmul", "sym"): [111006, 230250, 474964], ("barrettmul", "asym"): [107754, 231106, 495204],
    ("montlazy", "sym"): [105409, 197776, 391516], ("montlazy", "asym"): [88765, 178091, 350014],
    ("integrated", "sym"): [95534, 182711, 354126], ("integrated", "asym"): [82778, 161396, 315883],
}

def parse_meas(path, n):
    """returns list[test][prime] of printed 1000-iter averages"""
    vals = [int(x) for x in re.findall(r"Cycles:\)\) (\d+)", open(path, errors="replace").read())]
    np_ = NP[n]
    assert len(vals) == 9 * np_, (path, len(vals))
    return [vals[t * np_:(t + 1) * np_] for t in range(9)]

def mean(x): return sum(x) / len(x)

rows = []
res = {}
for c, _ in CFG:
    for m, _ in MODES:
        for n in NS:
            p = f"{R}/raw/meas/{c}_{m}_{n}.log"
            if not os.path.exists(p):
                continue
            t = parse_meas(p, n)
            all9 = mean([mean(pr) for pr in t])     # mean over 9 vectors of per-prime mean
            t0 = mean(t[0])                         # test vector 0 only, per-prime mean
            res[(c, m, n)] = dict(all9=all9, t0=t0, per=t)
            for ti, pr in enumerate(t):
                for pi, v in enumerate(pr):
                    rows.append([c, m, n, ti, pi, v])
with open(f"{R}/meas_per_test_prime.csv", "w", newline="") as f:
    w = csv.writer(f); w.writerow(["config", "mode", "N", "test", "prime_idx", "avg_cycles_1000iter"]); w.writerows(rows)

# per-iteration CSV
it_stats = {}
with open(f"{R}/iter_cycles.csv", "w", newline="") as f:
    w = csv.writer(f); w.writerow(["config", "mode", "N", "test", "prime_idx", "iter", "cycles"])
    for c, _ in CFG:
        for m, _ in MODES:
            for n in NS:
                p = f"{R}/raw/csv/{c}_{m}_{n}.log"
                if not os.path.exists(p):
                    continue
                allv = []
                for line in open(p, errors="replace"):
                    if line.startswith("CYC,"):
                        _, ti, pi, j, v = line.strip().split(",")
                        w.writerow([c, m, n, ti, pi, j, v]); allv.append(int(v))
                if allv:
                    s = sorted(allv)
                    it_stats[(c, m, n)] = dict(cnt=len(s), mean=mean(s), median=statistics.median(s),
                                               sd=statistics.pstdev(s), mn=s[0], p5=s[int(.05 * len(s))],
                                               p95=s[int(.95 * len(s)) - 1], mx=s[-1])

with open(f"{R}/summary.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["config", "mode", "N", "new_avg_all9", "new_speedup_all9", "new_avg_test0", "new_speedup_test0",
                "paper_m1", "paper_speedup", "delta_vs_paper_pct_all9",
                "csv_mean", "csv_median", "csv_sd", "csv_min", "csv_p5", "csv_p95", "csv_max", "csv_count"])
    for c, _ in CFG:
        for m, _ in MODES:
            for i, n in enumerate(NS):
                if (c, m, n) not in res: continue
                r = res[(c, m, n)]; b = res[("baseline", m, n)]
                pap = PAPER[(c, m)][i]; pb = PAPER[("baseline", m)][i]
                s = it_stats.get((c, m, n), {})
                w.writerow([c, m, n, round(r["all9"]), round(b["all9"] / r["all9"], 4), round(r["t0"]), round(b["t0"] / r["t0"], 4),
                            pap, round(pb / pap, 4), round((r["all9"] - pap) / pap * 100, 2),
                            *(round(s[k]) if k in s else "" for k in ("mean", "median", "sd", "mn", "p5", "p95", "mx")), s.get("cnt", "")])

def fmt(v): return f"{round(v):,}"
print("## Table A: all-9 metric")
print("| Configuration | Mode | N=1024 | N=2048 | N=4096 |\n|---|---|---:|---:|---:|")
for c, cn in CFG:
    for m, mn in MODES:
        cells = []
        for n in NS:
            r = res[(c, m, n)]["all9"]; b = res[("baseline", m, n)]["all9"]
            cells.append(fmt(r) if c == "baseline" else f"{fmt(r)} ({b / r:.2f}×)")
        print(f"| {cn} | {mn} | " + " | ".join(cells) + " |")
print("\n## Table B: new vs paper (all-9)")
print("| Configuration | Mode | N | 재측정 평균 | 재측정 speedup | 논문 Table 5 | 논문 speedup | 차이(사이클) |\n|---|---|---:|---:|---:|---:|---:|---:|")
for c, cn in CFG:
    for m, mn in MODES:
        for i, n in enumerate(NS):
            r = res[(c, m, n)]["all9"]; b = res[("baseline", m, n)]["all9"]
            pap = PAPER[(c, m)][i]; pb = PAPER[("baseline", m)][i]
            print(f"| {cn} | {mn} | {n} | {fmt(r)} | {b / r:.2f}× | {pap:,} | {pb / pap:.2f}× | {(r - pap) / pap * 100:+.1f}% |")
print("\n## Table C: test-0-only metric")
print("| Configuration | Mode | N=1024 | N=2048 | N=4096 |\n|---|---|---:|---:|---:|")
for c, cn in CFG:
    for m, mn in MODES:
        cells = []
        for n in NS:
            r = res[(c, m, n)]["t0"]; b = res[("baseline", m, n)]["t0"]
            cells.append(fmt(r) if c == "baseline" else f"{fmt(r)} ({b / r:.2f}×)")
        print(f"| {cn} | {mn} | " + " | ".join(cells) + " |")
print("\n## Table D: per-iteration stats")
print("| Configuration | Mode | N | 표본 수 | 평균 | 중앙값 | 표준편차 | min | p5 | p95 | max |\n|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
for c, cn in CFG:
    for m, mn in MODES:
        for n in NS:
            s = it_stats.get((c, m, n))
            if s:
                print(f"| {cn} | {mn} | {n} | {s['cnt']:,} | {fmt(s['mean'])} | {fmt(s['median'])} | {fmt(s['sd'])} | {fmt(s['mn'])} | {fmt(s['p5'])} | {fmt(s['p95'])} | {fmt(s['mx'])} |")
print("\n## spread across 9 test vectors (max/min of per-test averages)")
for c, _ in CFG:
    for m, _ in MODES:
        for n in NS:
            per = [mean(p) for p in res[(c, m, n)]["per"]]
            print(f"{c} {m} {n}: min={min(per):.0f} max={max(per):.0f} spread={(max(per)-min(per))/mean(per)*100:.1f}%")
