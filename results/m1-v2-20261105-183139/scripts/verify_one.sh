#!/bin/bash
d="$1"; name=$(basename "$d"); v=$(basename $(dirname "$d"))
n=${name##*_}; np=1; [ "$n" = 4096 ] && np=3
cd "$d" && ./build/bin/seal_embedded_tests > verify_run.log 2>&1; rc=$?
ok=$(grep -c "^VERIFY_OK" verify_run.log); mm=$(grep -c "Mismatched index\|DECODE_MISMATCH\|Assertion failed" verify_run.log)
done_=$(grep -c "All tests passed" verify_run.log)
echo "$v,$name,$rc,$ok,$((9*np)),$mm,$done_"
