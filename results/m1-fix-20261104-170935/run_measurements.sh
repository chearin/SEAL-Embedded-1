#!/bin/bash
# Run with:  sudo bash /Users/gimchaelin/Desktop/git/SEAL-Embedded-1/results/m1-fix-20261104-170935/run_measurements.sh
# Executes the 24 measurement binaries (unchanged harness: 5 warm-up + 1000 timed calls, m1cycles.c kperf counter)
# then the 24 per-iteration-CSV binaries. Each binary runs from its own tree dir (needs ./adapter_output_data).
set -u
T=/private/tmp/claude-501/-Users-gimchaelin-Desktop-git-SEAL-Embedded-1/acaf6c67-a1cf-4827-964f-2c07567ac451/scratchpad/trees
OUT=/Users/gimchaelin/Desktop/git/SEAL-Embedded-1/results/m1-fix-20261104-170935/raw
if [ "$(id -u)" != 0 ]; then echo "must run with sudo (kperf counters need root)"; exit 1; fi
{ date; sw_vers; sysctl -n machdep.cpu.brand_string; pmset -g ps | head -2; uptime; } > $OUT/env.txt 2>&1
for v in meas csv; do
  for c in baseline barrettmul montlazy integrated; do
    for m in sym asym; do
      for n in 1024 2048 4096; do
        d=$T/$v/${c}_${m}_${n}
        echo "[$v] ${c}_${m}_${n}"
        ( cd $d && ./build/bin/seal_embedded_tests > $OUT/$v/${c}_${m}_${n}.log 2>&1 )
        echo "  exit=$?  avg-lines=$(grep -c 'Cycles:))' $OUT/$v/${c}_${m}_${n}.log)"
      done
    done
  done
done
date >> $OUT/env.txt
chown -R ${SUDO_USER:-$(stat -f %Su $OUT)} $OUT
echo DONE
