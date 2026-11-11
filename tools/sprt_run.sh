#!/usr/bin/env bash
# Sequential SPRT of agent A (the change) vs. B (the previous version), ADR-007 §5: batches of one seat-swapped
# pair per level (tools/tournament, own tournament id per batch, so fresh game seeds), tools/sprt.py over all
# batches after each one, until H1 or H0 is accepted or the 2,000-pair cap is reached. Rerunning resumes
# (finished batches are skipped by the runner's resume).
#
# Usage (repo root): tools/sprt_run.sh <name> <agent A> <agent B> [levels dir] [obs] [jobs]
#   defaults: build/levels/dev, partial (the Fog condition), 8 jobs. Log: data/results/sprt-<name>.log
set -euo pipefail
name=$1 a=$2 b=$3 levels=${4:-build/levels/dev} obs=${5:-partial} jobs=${6:-8}
log=data/results/sprt-$name.log
test -d "$levels" || { echo "sprt_run: no levels in $levels (make tournament SEEDS=... exports them)" >&2; exit 1; }
echo "sprt $name: A=$a B=$b levels=$levels obs=$obs git $(git rev-parse --short HEAD)$(git diff --quiet || echo -dirty)" >> "$log"
for k in $(seq 1 100); do
    id=sprt-$name-b$k
    build/release/tournament --levels="$levels" --agents="$a,$b" --reps=1 --obs="$obs" --id="$id" \
        --out="data/results/$id.csv" --jobs="$jobs" > /dev/null
    set +e
    .venv/bin/python tools/sprt.py "$a" "$b" data/results/sprt-"$name"-b*.csv >> "$log"
    code=$?
    set -e
    echo "batch $k done $(date -Is), sprt exit $code" >> "$log"
    if [ "$code" -ne 2 ]; then
        exit "$code"
    fi
done
