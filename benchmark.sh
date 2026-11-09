#!/usr/bin/env bash

export OMP_WAIT_POLICY=active

b_start=$(date +%s%N)
for ((i = 0; i < 10 || total < 10000000000; i++)); do
    start=$(date +%s%N)
    ./sandpile.exe $1 > /dev/null || exit $?
    end=$(date +%s%N)

    elapsed=$((end - start))

    now=$(date +%s%N)
    total=$((now - b_start))

    if ((i == 0 || elapsed < min)); then
        min=$elapsed
    fi

    ((total > 180000000000)) && break
done
b_end=$(date +%s%N)

tot_elapsed=$((b_end - b_start))

printf 'Best: %d.%03d s\n' "$((min / 1000000000))" "$(((min % 1000000000) / 1000000))"
printf 'Overall: %d.%03d s\n' "$((tot_elapsed / 1000000000))" "$(((tot_elapsed % 1000000000) / 1000000))"
