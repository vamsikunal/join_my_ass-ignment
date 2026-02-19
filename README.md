# Distributed Hashcash Proof-of-Work System

CS740 Assignment — Distributed Systems Lab  
**Author:** vamsikunal

---

## What This Is

A real distributed Proof-of-Work system using the original [hashcash](http://www.hashcash.org/) C library over a LAN.

- One machine runs the **Coordinator** (manages rounds, never mines)
- Any number of Linux machines can join as **Workers** with a single `wget` command
- Difficulty starts at `10` bits and increments forever — no upper cap
- Every solved round is recorded to `results/results.csv`
- Beautiful ANSI terminal UI on both coordinator and worker

---

## Architecture

```
Coordinator (master)
   │  TCP :9000  ─── WORK|bits ──►  Worker 1 (12 threads)
   │             ◄── FOUND|stamp ─  Worker 1
   │  TCP :9000  ─── WORK|bits ──►  Worker 2 (12 threads)
   │             ◄── FOUND|stamp ─  Worker 2
   │  HTTP :8000 ──────────────────► (serves worker binary + results.csv)
```

- Workers receive a `WORK|bits` message, spawn 12 threads, each calls `hashcash_mint()`
- The first thread to find a valid stamp sends `FOUND|stamp` to the coordinator
- Coordinator verifies with `hashcash_count()`, logs to CSV, broadcasts `STOP`
- Next round begins at difficulty + 1
- New workers joining mid-round are included from the **next** round automatically

---

## Quick Start

### On the Coordinator Machine

```bash
git clone --recurse-submodules https://github.com/vamsikunal/join_my_ass-ignment.git
cd join_my_ass-ignment
bash scripts/serve.sh 9000
```

This will:
1. Build `coordinator` and `worker` binaries
2. Start an HTTP server on port `8000` (serves binaries to workers)
3. Launch the coordinator TUI

### On Any Worker Machine (One Command)

Replace `192.168.1.10` with your coordinator's actual LAN IP:

```bash
wget -qO- https://raw.githubusercontent.com/vamsikunal/join_my_ass-ignment/main/scripts/join.sh \
    | bash -s -- 192.168.1.10 9000
```

This will:
1. Download the pre-built `worker` binary from the coordinator
2. Sync the current `results.csv`
3. Launch the worker TUI and connect immediately

---

## Protocol

| Direction             | Message          | Meaning                          |
|-----------------------|------------------|----------------------------------|
| Coordinator → Worker  | `WORK\|bits\n`   | Start mining at this difficulty  |
| Coordinator → Worker  | `STOP\n`         | Abort current round              |
| Coordinator → Worker  | `PING\n`         | Heartbeat check                  |
| Worker → Coordinator  | `READY\n`        | Connected, waiting for work      |
| Worker → Coordinator  | `FOUND\|stamp\n` | Valid stamp found                |
| Worker → Coordinator  | `PONG\n`         | Heartbeat reply                  |

---

## Result File

`results/results.csv` — appended after every solved round.

```
difficulty,stamp,bits_verified,time_ms,winner_ip,active_workers,est_hashrate,timestamp
10,"1:10:260219:distributed-pow-lab::...",10,18,192.168.1.11,3,48200000,2026-02-19 11:04:01
11,"1:11:260219:distributed-pow-lab::...",11,44,192.168.1.12,3,47100000,2026-02-19 11:04:02
```

Workers receive a copy of this file at join time and save solved rounds to  
`~/distributed-pow/results.csv` locally.

---

## Project Structure

```
distributed-pow/
├── hashcash/                   ← git submodule: hashcash-org/hashcash (c/)
├── common/
│   ├── protocol.h              ← message constants, structs, config
│   ├── logger.h / logger.c     ← CSV result writer (thread-safe)
│   └── tui.h                   ← ANSI terminal UI helpers
├── coordinator/
│   └── coordinator.c           ← master: accept, difficulty loop, TUI
├── worker/
│   └── worker.c                ← miner: 12 threads, hashcash_mint, TUI
├── scripts/
│   ├── serve.sh                ← coordinator setup + launch
│   └── join.sh                 ← worker one-command join
├── results/
│   └── results.csv             ← master result log
└── Makefile
```

---

## Build (Manual)

Requires: `gcc`, `make`, `python3`, `wget`, `pthread`

```bash
# Standard build
make

# Optimized for host CPU (recommended on coordinator)
make fast

# Clean
make clean
```

---

## Worker Threads

Each worker spawns **12 threads** (matching 6-core × 2 hardware-thread lab CPUs).  
Each thread calls `hashcash_mint()` independently with a random counter start —  
no explicit nonce range partitioning needed. The library's callback mechanism  
provides clean remote-stop within ~100ms of the coordinator sending `STOP`.

---

## Hashcash Stamp Format

```
1:bits:YYMMDD:distributed-pow-lab::ext:counter
│  │    │      │                         └─ base64 nonce (random start)
│  │    │      └─ fixed resource string
│  │    └─ UTC date
│  └─ difficulty (leading SHA-1 zero bits)
└─ version 1
```

Stamps are fully verifiable by anyone using the standard hashcash tool:
```bash
./hashcash -c -b 20 -r distributed-pow-lab <stamp>
```
