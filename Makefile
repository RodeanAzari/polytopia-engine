# Polytopia engine: see CLAUDE.md §3. Targets not yet implemented fail loudly.
TRIBES_REPO   := https://github.com/GAIGResearch/Tribes.git
TRIBES_COMMIT := 270e7e564630e5ef3dcc04c96dd66f58ef7273f3
GEN := $(shell command -v ninja >/dev/null 2>&1 && echo "-G Ninja")
JOBS ?= $(shell nproc)

.PHONY: replay-game setup setup-nn build-nn tidy-nn oracle-smoke oracle-equiv oracle-log difftest-init difftest-mindbender difftest-batched difftest-fm build test tidy format clean difftest bench replay tournament selfplay train plots plots-headline ci

# Phase 4: PyTorch with CUDA into the same venv, and LibTorch (same version) for C++ inference in
# .deps/ (ADR-012 §1; ~3 GB, never committed). LibTorch ships cuDNN's front library only: its
# sub-libraries come from the pip wheel, byte-identical front library checked. Not run by CI.
LIBTORCH := .deps/libtorch-2.14.0-cu130
LIBTORCH_URL := https://download-r2.pytorch.org/libtorch/cu130/libtorch-shared-with-deps-2.14.0%2Bcu130.zip
CUDNN_PIP = $(shell .venv/bin/python -c "import nvidia.cudnn, os; print(os.path.join(nvidia.cudnn.__path__[0], 'lib'))")
setup-nn:
	python3 -m venv .venv && .venv/bin/pip install -q -r requirements-nn.txt
	.venv/bin/python -c "import torch; assert torch.cuda.is_available(), 'no CUDA'; print(torch.__version__, torch.cuda.get_device_name(0))"
	@if [ ! -d $(LIBTORCH) ]; then mkdir -p .deps && curl -sL -o .deps/libtorch.zip $(LIBTORCH_URL) && \
	  (cd .deps && unzip -q libtorch.zip && mv libtorch $(notdir $(LIBTORCH)) && rm libtorch.zip); fi
	cmp $(CUDNN_PIP)/libcudnn.so.9 $(LIBTORCH)/lib/libcudnn.so.9
	for f in $(CUDNN_PIP)/libcudnn_*.so.9; do ln -sf $$f $(LIBTORCH)/lib/; done

# The build with the LibTorch evaluator (selfplay --net, net_check, tournament with
# PUCT:...:net=<model.ts> agents) in build/torch.
build-nn:
	cmake -S . -B build/torch $(GEN) -DCMAKE_BUILD_TYPE=Release -DPOLY_TORCH=ON
	cmake --build build/torch -j$(JOBS) --target selfplay net_check tournament

# Tribes is unlicensed: cloned locally at a pinned commit, never committed (ADR-002).
setup:
	@if [ ! -d oracle/tribes ]; then git clone -q $(TRIBES_REPO) oracle/tribes; fi
	git -C oracle/tribes checkout -q $(TRIBES_COMMIT)
	@echo "Tribes pinned at $(TRIBES_COMMIT)"
	python3 -m venv .venv && .venv/bin/pip install -q -r requirements.txt
	mkdir -p build/tribes
	javac -nowarn -encoding UTF-8 -cp oracle/tribes/lib/json.jar -d build/tribes \
	  $$(find oracle/tribes/src oracle/bridge -name '*.java')
	@# Partial-obs oracle (ADR-004): a build copy with only PLAY_WITH_FULL_OBS = false.
	rm -rf build/tribes-po-src build/tribes-po && cp -r oracle/tribes/src build/tribes-po-src
	@test "$$(grep -c 'PLAY_WITH_FULL_OBS = true;' build/tribes-po-src/core/Constants.java)" = 1 \
	  || { echo "setup: PLAY_WITH_FULL_OBS line not found exactly once" >&2; exit 1; }
	sed -i 's/PLAY_WITH_FULL_OBS = true;/PLAY_WITH_FULL_OBS = false;/' build/tribes-po-src/core/Constants.java
	mkdir -p build/tribes-po
	javac -nowarn -encoding UTF-8 -cp oracle/tribes/lib/json.jar -d build/tribes-po \
	  $$(find build/tribes-po-src oracle/bridge -name '*.java')

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

# OBS=partial runs the three difftests below against the partial-obs oracle build (ADR-004); logs go
# to separate *-po directories.
OBS ?= full
PO := $(if $(filter partial,$(OBS)),-po,)
OBSFLAG := $(if $(filter partial,$(OBS)),--partial,)
comma := ,

# Map-load difftest: N oracle initial states (2..8 players, all tribes) vs. our loader + dump (05 §1).
difftest-init: build
	rm -rf build/oracle-logs/init$(PO)
	python3 oracle/scripts/gen_init_corpus.py $(N) build/oracle-logs/init$(PO) $(OBSFLAG)
	build/release/oracle_diff build/oracle-logs/init$(PO)/*.log

# Replay difftest: N whole oracle games (2..8 players, both modes, Random / Rule Based), replayed
# action by action; every state, every legal-action set, the end tick and each tribe's result and
# score must match.
# Logs are ~40 MB per game (build/oracle-logs/full, regenerated each run).
difftest: build
	rm -rf build/oracle-logs/full$(PO)
	python3 oracle/scripts/gen_init_corpus.py $(N) build/oracle-logs/full$(PO) --full $(OBSFLAG)
	build/release/oracle_diff build/oracle-logs/full$(PO)/*.log

# Mind Bender scenario (Heal others, Convert never occur in normal games): every tribe starts with
# Philosophy, our MindBender agent vs. Random, 2..4 players. ~5 MB per game.
difftest-mindbender: build
	rm -rf build/oracle-logs/mindbender$(PO)
	python3 oracle/scripts/gen_init_corpus.py $(N) build/oracle-logs/mindbender$(PO) --full \
	  --scenario=mindbender $(OBSFLAG)
	build/release/oracle_diff build/oracle-logs/mindbender$(PO)/*.log

# Replay difftest at scale (Phase 1 DoD: >= 1,000 games): same games as `make difftest N=`, generated,
# replayed and deleted BATCH at a time; failing logs kept in build/oracle-logs/batched$(PO)/failed/.
BATCH ?= 32
difftest-batched: build
	python3 oracle/scripts/difftest_batched.py $(N) build/oracle-logs/batched$(PO) --batch=$(BATCH) $(OBSFLAG)

# Agent-side forward model (rule facts 04 §9; what Tribes' search agents simulate): the batched replay
# difftest plus, every PROBE-th decision, 30 steps of Tribes' advance() on a copy of the observation,
# each compared with our agent_advance (state and legal actions). OBS=partial and SCENARIO= as above.
PROBE ?= 25
SCENARIO ?=
difftest-fm: build
	python3 oracle/scripts/difftest_batched.py $(N) build/oracle-logs/fm$(PO) --batch=$(BATCH) --probe=$(PROBE) \
	  $(OBSFLAG) $(if $(SCENARIO),--scenario=$(SCENARIO),)

# Simulator speed vs. Tribes' forward model (Phase 1 DoD): uniform-random playouts on the same levels,
# N games per side for 2/4/8 players x Capitals/Score -> data/results/bench-<stamp>.{csv,md}.
bench: build
	python3 oracle/scripts/bench.py $(N)

# One recorded random game for the viewer (tools/viewer/index.html): Tribes generates the level
# (OracleBench, untimed use), tools/record.cpp plays and records it. OBS=partial records fog of war.
REPLAY_PLAYERS ?= 4
REPLAY_MODE ?= Capitals
REPLAY_SEED ?= 40000
REPLAY_LEVEL := build/replays/levels/level-$(REPLAY_PLAYERS)-$(REPLAY_MODE)-$(REPLAY_SEED).txt
replay: build
	@test -f $(REPLAY_LEVEL) || (cd oracle/tribes && java -cp ../../build/tribes:lib/json.jar core.game.OracleBench \
	  $(REPLAY_PLAYERS) $(REPLAY_MODE) $(REPLAY_SEED) 1 0 ../../build/replays/levels > /dev/null)
	build/release/record $(REPLAY_LEVEL) build/replays/game-$(REPLAY_PLAYERS)-$(REPLAY_MODE)-$(REPLAY_SEED)-$(OBS).json $(OBSFLAG)
	@echo "open tools/viewer/index.html in a browser and choose that file"

# One recorded agent game, re-played from a tournament CSV row (its level file, agents in seat order
# and game seed reproduce that game; tools/agents.hpp). PUCT decisions carry root visit counts (vc).
#   make replay-game LEVEL=build/levels/test/level-....txt AGENTS=PUCT,RHEA SEED=<gameSeed> [OBS=partial]
replay-game: build
	build/release/record $(LEVEL) build/replays/game-$(subst :,_,$(subst $(comma),-vs-,$(AGENTS)))-$(SEED)-$(OBS).json $(OBSFLAG) --agents=$(AGENTS) --seed=$(SEED)
	@echo "serve the repo (python3 -m http.server) and open tools/viewer/index.html?src=../../<that file>"

# Speed chart from repeat bench runs (default: every data/results/bench-*.csv; pass BENCH_CSV= to choose).
# docs/buildplan.md §13.2: dated name; the sidecar .md next to it is written by hand.
BENCH_CSV ?= $(sort $(wildcard data/results/bench-*.csv))
plots:
	@test -n "$(BENCH_CSV)" || { echo "plots: no data/results/bench-*.csv; run make bench" >&2; exit 1; }
	.venv/bin/python tools/plot_bench.py docs/media/$(shell date +%F)-sim-speed.png $(BENCH_CSV)

# Phase 3 headline chart from tools/headline.sh's CSVs (prints the table and the "beats every baseline" check);
# TAG= picks a tagged run (tools/headline.sh <jobs> <tag>), e.g. TAG=-tuned
TAG ?=
plots-headline:
	.venv/bin/python tools/plot_headline.py docs/media/$(shell date +%F)-p3-headline$(TAG).png \
	  data/results/p3-headline$(TAG)-full-*.csv data/results/p3-headline$(TAG)-partial-*.csv

build:
	cmake -S . -B build/release $(GEN) -DCMAKE_BUILD_TYPE=Release
	cmake --build build/release -j$(JOBS)
	cmake -S . -B build/debug $(GEN) -DCMAKE_BUILD_TYPE=Debug -DPOLY_SANITIZE=ON
	cmake --build build/debug -j$(JOBS)

test: build
	ctest --test-dir build/release --output-on-failure
	ctest --test-dir build/debug --output-on-failure
	.venv/bin/python -m pytest -q tests/python

tidy: build
	clang-tidy -p build/release --quiet $(filter-out $(TORCH_SRC),$(shell find sim search selfplay tests tools -name '*.cpp'))

# LibTorch sources: only in the POLY_TORCH build (make build-nn), linted against it.
TORCH_SRC := selfplay/torch_evaluator.cpp tools/net_check.cpp
tidy-nn: build-nn
	clang-tidy -p build/torch --quiet $(TORCH_SRC) tools/selfplay.cpp

format:
	clang-format -i $(shell find sim search selfplay tests tools -name '*.hpp' -o -name '*.cpp')

ci: test tidy

clean:
	rm -rf build

# Round-robin of our agents on our simulator (ADR-007): seat-swapped pairs, REPS per level and
# seating. Levels come from Tribes' generator (export_levels.py -> build/levels/, never committed).
# -> data/results/$(ID).csv and .md (W/D/L, Wilson 95%). Resumable: rerun the same command.
AGENTS ?= Random,RuleBased
REPS ?= 10
SEEDS ?= data/seeds/paper-25.txt
ID ?= ours-$(OBS)-$(shell date +%Y%m%d)
LEVELS := build/levels/$(basename $(notdir $(SEEDS)))
tournament: build
	test -d $(LEVELS) || python3 oracle/scripts/export_levels.py $(SEEDS) $(LEVELS)
	build/release/tournament --levels=$(LEVELS) --agents=$(AGENTS) --reps=$(REPS) --obs=$(OBS) \
	  --id=$(ID) --out=data/results/$(ID).csv --jobs=$(JOBS)
	python3 oracle/scripts/tournament_summary.py data/results/$(ID).csv \
	  "--command=make tournament AGENTS=$(AGENTS) REPS=$(REPS) SEEDS=$(SEEDS) OBS=$(OBS) ID=$(ID)"

# Phase 4 training records (ADR-011 §4, ADR-012 §2): the agent plays itself on the training levels
# (data/seeds/train.txt), Paper and Fog alternating. It runs a frozen copy of the binary
# (build/runs/<git>/), so rebuilding build/release doesn't touch a running job. Resumable: rerun the
# same command. -> data/selfplay/$(RUN)/<game>.rec + games.csv (never committed).
RUN ?= bootstrap
GAMES ?= 2000
SP_AGENT ?= PUCT
SP_BIN := build/runs/$(shell git rev-parse --short HEAD)/selfplay
selfplay: build
	test -d build/levels/train || python3 oracle/scripts/export_levels.py data/seeds/train.txt build/levels/train
	mkdir -p $(dir $(SP_BIN)) && cp build/release/selfplay $(SP_BIN)
	$(SP_BIN) --levels=build/levels/train --games=$(GAMES) --out=data/selfplay/$(RUN) --agent=$(SP_AGENT) \
	  --id=$(RUN) --jobs=$(JOBS)

# Supervised training on a run's records (ADR-011 §3-4): pack them (nn/pack.py), then nn/train.py on the
# GPU. -> data/selfplay/$(RUN)/train-$(TRAIN_TAG)/step-<k>.pt + log.csv. STEPS, TRAIN_TAG and TRAIN_ARGS optional.
STEPS ?= 20000
TRAIN_TAG ?= $(shell date +%Y%m%d-%H%M)
train:
	.venv/bin/python nn/pack.py data/selfplay/$(RUN)
	.venv/bin/python nn/train.py data/selfplay/$(RUN)/packed data/selfplay/$(RUN)/train-$(TRAIN_TAG) --steps $(STEPS) \
	  $(TRAIN_ARGS)
