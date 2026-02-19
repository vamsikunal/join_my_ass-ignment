#!/usr/bin/env bash
# join.sh — Worker join script (downloaded and piped through bash via wget)
# CS740 Assignment — vamsikunal/join_my_ass-ignment
#
# Usage (on worker machine):
#   wget -qO- https://raw.githubusercontent.com/vamsikunal/join_my_ass-ignment/main/scripts/join.sh \
#       | bash -s -- <coordinator_ip> <port>
#
# What this script does:
#   1. Creates ~/distributed-pow/ directory
#   2. Downloads pre-built worker binary from coordinator's HTTP server
#   3. Downloads latest results.csv from coordinator (syncs existing progress)
#   4. Launches the worker binary (TUI appears)

set -e

COORD_IP="${1}"
COORD_PORT="${2:-9000}"
HTTP_PORT=8000
WORK_DIR="$HOME/distributed-pow"

# ── Validate args ─────────────────────────────────────────────
if [ -z "$COORD_IP" ]; then
    echo "Usage: bash join.sh <coordinator_ip> [port]"
    echo "Example: bash join.sh 192.168.1.10 9000"
    exit 1
fi

COORD_HTTP="http://${COORD_IP}:${HTTP_PORT}"

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║     Distributed Hashcash PoW — Worker Join           ║"
echo "╚══════════════════════════════════════════════════════╝"
echo ""
echo "  Coordinator : ${COORD_IP}:${COORD_PORT}"
echo "  HTTP server : ${COORD_HTTP}"
echo "  Work dir    : ${WORK_DIR}"
echo ""

# ── Step 1: Create work directory ─────────────────────────────
echo "[1/4] Creating work directory..."
mkdir -p "$WORK_DIR/results"
echo "      $WORK_DIR — OK"

# ── Step 2: Download worker binary ────────────────────────────
echo "[2/4] Downloading worker binary..."
wget -q --show-progress \
    "${COORD_HTTP}/bin/worker" \
    -O "${WORK_DIR}/worker"
chmod +x "${WORK_DIR}/worker"
echo "      worker binary downloaded and made executable"

# ── Step 3: Sync results.csv ──────────────────────────────────
echo "[3/4] Syncing results.csv from coordinator..."
wget -q "${COORD_HTTP}/results/results.csv" \
    -O "${WORK_DIR}/results/results.csv" 2>/dev/null || {
    echo "      (no results.csv yet on coordinator — starting fresh)"
    mkdir -p "${WORK_DIR}/results"
}
echo "      results.csv synced"

# ── Step 4: Launch worker ─────────────────────────────────────
echo "[4/4] Launching worker..."
echo ""
echo "══════════════════════════════════════════════════════"
echo " Connecting to ${COORD_IP}:${COORD_PORT} ..."
echo "══════════════════════════════════════════════════════"
echo ""

cd "${WORK_DIR}"
exec ./worker "${COORD_IP}" "${COORD_PORT}"
