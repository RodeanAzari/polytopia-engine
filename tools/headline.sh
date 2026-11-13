#!/usr/bin/env bash
# Phase 3 headline (docs/buildplan.md §11, ADR-007 §3–§4; plan agreed with the user 2026-11-11): our PUCT
# (defaults: ADR-008 §8) against every baseline on the 100 test levels, one seat-swapped pair per level
# (200 games per cell), in the Paper (full obs) and Fog (partial obs) conditions; the flat variant
# (PUCT:macro=0) against Rule Based and RHEA for the macro vs. flat comparison. One tournament id per
# condition, so every cell of a condition plays the same game seeds per level. Rerunning resumes (the
# runner skips finished games). Log: data/results/p3-headline.log; one CSV + summary .md per cell.
#
# Usage (repo root, after `make build`): tools/headline.sh [jobs] [tag]   (default 8, no tag)
# A tag (e.g. -tuned) names a separate run's files (p3-headline<tag>-...) and log; the tournament ids stay
# the same, so a rerun after an engine change plays the same game seeds per level (paired comparison).
set -euo pipefail
jobs=${1:-8}
tag=${2:-}
levels=build/levels/test
log=data/results/p3-headline$tag.log
test -d "$levels" || { echo "headline: no levels in $levels (make tournament SEEDS=data/seeds/test.txt)" >&2; exit 1; }
hw="$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //'), $(nproc) threads"
echo "headline start $(date -Is) git $(git rev-parse --short HEAD)$(git diff --quiet || echo -dirty) jobs $jobs hw $hw" >> "$log"
cells=()
for opp in RHEA RuleBased MCTS OSLA MC Random; do cells+=("PUCT,$opp"); done
cells+=("PUCT:macro=0,RHEA" "PUCT:macro=0,RuleBased")
for obs in full partial; do
    for cell in "${cells[@]}"; do
        id=p3-headline-$obs
        out=data/results/p3-headline$tag-$obs-${cell//[,:=]/_}.csv
        build/release/tournament --levels="$levels" --agents="$cell" --reps=1 --obs="$obs" --id="$id" \
            --out="$out" --jobs="$jobs" > /dev/null
        python3 oracle/scripts/tournament_summary.py "$out" \
            "--command=tools/headline.sh $jobs $tag (cell $cell, obs $obs, id $id)" > /dev/null
        echo "cell $obs $cell done $(date -Is): $out" >> "$log"
    done
done
echo "headline end $(date -Is)" >> "$log"
