import csv, sys
old = {(r['config'], r['mode'], r['N']): r for r in csv.DictReader(open(sys.argv[1]))}
new = {(r['config'], r['mode'], r['N']): r for r in csv.DictReader(open(sys.argv[2]))}
names = {"baseline": "Baseline", "barrettmul": "Barrett Mul", "montlazy": "Mont.+Lazy NTT", "integrated": "Integrated"}
out = csv.writer(open(sys.argv[3], "w", newline=""))
out.writerow(["config","mode","N","m1fix_avg","m1fix_speedup","v2_avg","v2_speedup","delta_pct","m1fix_median","v2_median","delta_median_pct"])
print("| Configuration | Mode | N | m1-fix (원본) 평균 | m1-fix speedup | v2 세션 평균 | v2 세션 speedup | 사이클 변화 | 중앙값 변화 |")
print("|---|---|---:|---:|---:|---:|---:|---:|---:|")
for c in names:
    for m in ("sym", "asym"):
        for n in ("1024", "2048", "4096"):
            o, w = old[(c, m, n)], new[(c, m, n)]
            oa, wa = float(o['new_avg_all9']), float(w['new_avg_all9'])
            om, wm = float(o['csv_median']), float(w['csv_median'])
            d = (wa - oa) / oa * 100; dm = (wm - om) / om * 100
            out.writerow([c, m, n, round(oa), o['new_speedup_all9'], round(wa), w['new_speedup_all9'], round(d, 2), round(om), round(wm), round(dm, 2)])
            print(f"| {names[c]} | {m.upper()} | {n} | {oa:,.0f} | {float(o['new_speedup_all9']):.2f}× | {wa:,.0f} | {float(w['new_speedup_all9']):.2f}× | {d:+.1f}% | {dm:+.1f}% |")
