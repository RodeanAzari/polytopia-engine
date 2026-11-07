# Polytopia engine: see CLAUDE.md §3. Targets not yet implemented fail loudly.
TRIBES_REPO   := https://github.com/GAIGResearch/Tribes.git
TRIBES_COMMIT := 270e7e564630e5ef3dcc04c96dd66f58ef7273f3
GEN := $(shell command -v ninja >/dev/null 2>&1 && echo "-G Ninja")
JOBS ?= $(shell nproc)

.PHONY: setup oracle-smoke oracle-equiv oracle-log difftest-init build test tidy format clean difftest bench tournament selfplay train plots ci

# Tribes is unlicensed: cloned locally at a pinned commit, never committed (ADR-002).
setup:
	@if [ ! -d oracle/tribes ]; then git clone -q $(TRIBES_REPO) oracle/tribes; fi
	git -C oracle/tribes checkout -q $(TRIBES_COMMIT)
	@echo "Tribes pinned at $(TRIBES_COMMIT)"
	mkdir -p build/tribes
	javac -nowarn -encoding UTF-8 -cp oracle/tribes/lib/json.jar -d build/tribes \
	  $$(find oracle/tribes/src oracle/bridge -name '*.java')

# Same fixed seeds twice -> identical output, or the oracle is useless for difftests.
ORACLE_RUN := cd oracle/tribes && java -cp ../../build/tribes:lib/json.jar OracleRun
oracle-smoke:
	$(ORACLE_RUN) 1590191438878 1 2 Capitals "Random,Rule Based" "Xin Xi,Imperius" > ../../build/smoke1.txt
	$(ORACLE_RUN) 1590191438878 1 2 Capitals "Random,Rule Based" "Xin Xi,Imperius" > ../../build/smoke2.txt
	cmp build/smoke1.txt build/smoke2.txt && echo "oracle-smoke: reproducible"

# Our logging driver must reproduce stock Game.run (same final tick, results, scores) on N seeds per config.
N ?= 25
oracle-equiv:
	python3 oracle/scripts/check_loop_equivalence.py $(N)

# One full difftest log (level, seats, RNG draws, legal actions, actions, states) -> build/oracle-logs/.
# Defaults: LEVEL=1001 SEED=1 MODE=Capitals PLAYERS="Random,Rule Based" TRIBES="Xin Xi,Imperius"
LEVEL ?= 1001
SEED ?= 1
MODE ?= Capitals
PLAYERS ?= Random,Rule Based
TRIBES ?= Xin Xi,Imperius
oracle-log:
	mkdir -p build/oracle-logs
	cd oracle/tribes && java -cp ../../build/tribes:lib/json.jar OracleLog $(LEVEL) $(SEED) $(SEED) $(MODE) \
	  "$(PLAYERS)" "$(TRIBES)" > ../../build/oracle-logs/game-$(LEVEL)-$(SEED)-$(MODE).log

# Map-load difftest: N oracle initial states (2..8 players, all tribes) vs. our loader + dump (05 §1).
difftest-init: build
	python3 oracle/scripts/gen_init_corpus.py $(N) build/oracle-logs/init
	build/release/oracle_diff build/oracle-logs/init/*.log

# Replay difftest: N whole oracle games (2..8 players, both modes, Random / Rule Based), replayed
# action by action; every state, every legal-action set, the end tick and each tribe's result and
# score must match.
# Logs are ~40 MB per game (build/oracle-logs/full, regenerated each run).
difftest: build
	rm -rf build/oracle-logs/full
	python3 oracle/scripts/gen_init_corpus.py $(N) build/oracle-logs/full --full
	build/release/oracle_diff build/oracle-logs/full/*.log

build:
	cmake -S . -B build/release $(GEN) -DCMAKE_BUILD_TYPE=Release
	cmake --build build/release -j$(JOBS)
	cmake -S . -B build/debug $(GEN) -DCMAKE_BUILD_TYPE=Debug -DPOLY_SANITIZE=ON
	cmake --build build/debug -j$(JOBS)

test: build
	ctest --test-dir build/release --output-on-failure
	ctest --test-dir build/debug --output-on-failure

tidy: build
	clang-tidy -p build/release --quiet $(shell find sim tests tools -name '*.cpp')

format:
	clang-format -i $(shell find sim search selfplay tests tools -name '*.hpp' -o -name '*.cpp')

ci: test tidy

clean:
	rm -rf build

bench tournament selfplay train plots:
	@echo "make $@: not implemented yet" >&2; exit 1
