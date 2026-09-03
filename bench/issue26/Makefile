# bench/issue26/Makefile — reproduction entrypoints for
# https://github.com/Losera/incant-audio/issues/26
#
#   make verify        re-derive the published numbers from committed data (~1s)
#   make rederive      re-run the diagnostic-quality half (needs faust + faust-rs)
#   make replay        run the paired A/B with your own model (see vars below)
#   make test          the unit tests for the harness
#   make docker        build the pinned repro image (tagged with the short SHA)
#   make docker-verify           verify, in the container (rebuilds if stale)
#   make docker-replay           replay + score, in the container (LIMIT=N for a smoke run)
#
# replay / docker-replay knobs (override on the command line):
#   BACKEND=ollama|openai      ENDPOINT=<url>      MODEL=<name>
#   ARMS=A,B,C                 SAMPLES=1           OUT=<name>   (basename only)
#   API_KEY_ENV=<env var>      (openai backend)
#   LIMIT=<N>    XOR   STRATIFY=<N>   — first N distinct programs, or N
#                                       first-error-class-proportional. Setting
#                                       both is an error.
#   e.g.  make replay BACKEND=openai ENDPOINT=https://api.groq.com/openai/v1 \
#           MODEL=llama-3.3-70b-versatile API_KEY_ENV=GROQ_API_KEY ARMS=A,B SAMPLES=5
#   e.g.  make docker-replay LIMIT=20        # 20-program smoke run, then scores it
#
# Apple Silicon: archlinux has no arm64 image. Prefix with
#   DOCKER_DEFAULT_PLATFORM=linux/amd64
# (docker honours the env var; the build runs emulated).
# macOS ollama binds 127.0.0.1 — start it with OLLAMA_HOST=0.0.0.0 ollama serve.

ROOT       := $(shell git -C $(CURDIR) rev-parse --show-toplevel 2>/dev/null || echo $(CURDIR)/../..)
PYTHON     ?= python3
CORPUS     ?= $(ROOT)/bench/corpora/repair_corpus_20260830.json
BACKEND    ?= ollama
ENDPOINT   ?= http://localhost:11434
MODEL      ?= qwen2.5-coder:3b
ARMS       ?= A,B,C
SAMPLES    ?= 1
OUT        ?= repair_ab_standalone.json
API_KEY_ENV ?=
LIMIT      ?=
STRATIFY   ?=

# The image is tagged with the source short-SHA plus a "-dirty" suffix when the
# tree has uncommitted changes, so `docker-verify` can never silently run a
# stale build (a plain :latest could, and did).
_SHA   := $(shell git -C $(ROOT) rev-parse --short HEAD 2>/dev/null || echo nogit)
_DIRTY := $(shell git -C $(ROOT) status --porcelain 2>/dev/null | grep -q . && echo -dirty)
IMAGE  ?= incant-issue26:$(_SHA)$(_DIRTY)

_KEYENV := $(if $(API_KEY_ENV),--api-key-env $(API_KEY_ENV),)
_SUBSET := $(if $(LIMIT),--limit $(LIMIT),) $(if $(STRATIFY),--stratify $(STRATIFY),)
_OUTB   := $(notdir $(OUT))

.PHONY: verify rederive replay test docker docker-verify docker-replay clean

verify:
	$(PYTHON) $(CURDIR)/verify.py

rederive:
	PLUGINFORGE_FAUST_RS_BIN=$${PLUGINFORGE_FAUST_RS_BIN:-$$(command -v faust-rs)} \
	$(PYTHON) $(ROOT)/bench/frs_rederive_issue26.py

replay:
	$(PYTHON) $(CURDIR)/repair_ab_standalone.py \
	  --corpus $(CORPUS) --backend $(BACKEND) --endpoint $(ENDPOINT) \
	  --model $(MODEL) --arms $(ARMS) --samples $(SAMPLES) $(_KEYENV) $(_SUBSET) --out $(OUT)
	$(PYTHON) $(ROOT)/bench/score_repair_ab.py $(OUT) --screen $(CORPUS)

test:
	cd $(ROOT) && PLUGINFORGE_PROVIDER=anthropic $(PYTHON) -m pytest \
	  tests/test_repair_ab_core.py tests/test_frs_check_unit.py \
	  tests/test_fidelity_gate.py tests/test_corpus_screen.py \
	  tests/test_score_repair_ab.py -q

docker:
	docker build -f $(CURDIR)/Dockerfile -t $(IMAGE) \
	  --build-arg SOURCE_COMMIT=$(_SHA) \
	  --build-arg SOURCE_DIRTY=$$(git -C $(ROOT) status --porcelain | grep -q . && echo dirty || echo clean) \
	  $(ROOT)

docker-verify: docker
	docker run --rm $(IMAGE) verify

docker-replay: docker
	mkdir -p $(CURDIR)/out
	docker run --rm --user $$(id -u):$$(id -g) -e HOME=/tmp \
	  --add-host host.docker.internal:host-gateway \
	  $(if $(API_KEY_ENV),-e $(API_KEY_ENV),) -v $(CURDIR)/out:/out $(IMAGE) replay \
	  --backend $(BACKEND) --endpoint $(ENDPOINT) --model $(MODEL) \
	  --arms $(ARMS) --samples $(SAMPLES) $(_KEYENV) $(_SUBSET) --out /out/$(_OUTB)
	docker run --rm --user $$(id -u):$$(id -g) -e HOME=/tmp \
	  -v $(CURDIR)/out:/out $(IMAGE) score /out/$(_OUTB)

clean:
	rm -f $(CURDIR)/repair_ab_standalone*.json
	rm -rf $(CURDIR)/out
