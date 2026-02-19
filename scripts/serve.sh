#!/usr/bin/env bash
# serve.sh — Start coordinator: build binaries + HTTP file server + coordinator TUI
# CS740 Assignment — vamsikunal/join_my_ass-ignment
#
# Usage:
#   bash scripts/serve.sh [port]
#
# Default port: 9000
# HTTP server: port 8000 (serves the project directory so workers can wget binaries)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PORT="${1:-9000}"
HTTP_PORT=8000

cd "$PROJECT_DIR"

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║     Distributed Hashcash PoW — Coordinator Setup     ║"
echo "╚══════════════════════════════════════════════════════╝"
echo ""

# ── Step 1: Check dependencies ────────────────────────────────
echo "[1/4] Checking dependencies..."
for cmd in gcc make python3 wget; do
    if ! command -v $cmd &>/dev/null; then
        echo "ERROR: '$cmd' not found. Please install it first."
        exit 1
    fi
done
echo "      gcc, make, python3, wget — OK"

# ── Step 2: Check hashcash submodule ──────────────────────────
echo "[2/4] Checking hashcash source..."
if [ ! -f "$PROJECT_DIR/hashcash/c/hashcash.h" ]; then
    echo "      hashcash submodule not found. Initializing..."
    git submodule update --init --recursive
fi
echo "      hashcash/c/ — OK"

# ── Step 3: Build ─────────────────────────────────────────────
echo "[3/4] Building coordinator and worker binaries..."
make fast
echo "      Build complete — bin/coordinator, bin/worker ready"

# ── Step 4: Start HTTP file server ────────────────────────────
echo "[4/4] Starting HTTP file server on port $HTTP_PORT ..."
echo "      Workers can join with:"
echo ""
# Detect local IP
LOCAL_IP=$(hostname -I | awk '{print $1}')
echo "      wget -qO- https://raw.githubusercontent.com/vamsikunal/join_my_ass-ignment/main/scripts/join.sh | bash -s -- ${LOCAL_IP} ${PORT}"
echo ""

# Kill any existing python3 http.server on that port
pkill -f "python3 -m http.server $HTTP_PORT" 2>/dev/null || true

python3 -m http.server $HTTP_PORT --directory "$PROJECT_DIR" &>/dev/null &
HTTP_PID=$!
echo "      HTTP server PID: $HTTP_PID  (serving $PROJECT_DIR)"

# Trap to kill HTTP server on exit
trap "kill $HTTP_PID 2>/dev/null; echo 'HTTP server stopped.'" EXIT INT TERM

echo ""
echo "══════════════════════════════════════════════════════"
echo " Starting coordinator on port $PORT ..."
echo "══════════════════════════════════════════════════════"
echo ""

# ── Launch coordinator (foreground — TUI takes over) ──────────
"$PROJECT_DIR/bin/coordinator" "$PORT"
