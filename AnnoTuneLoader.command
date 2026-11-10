#!/bin/bash
# Double-click this file on macOS. The first Run downloads llama.cpp b12180.
# Nothing else is installed.
set -u
cd "$(dirname "$0")"
ROOT="$(pwd)"
CONFIG="${HOME}/Library/Application Support/AnnoTuneLoader"
CACHE="${CONFIG}/b12180"
mkdir -p "$CONFIG" "$CACHE"
PATHS="${CONFIG}/paths.txt"
LOG="${CONFIG}/server.log"
ERR="${CONFIG}/server.err"
PIDFILE="${CONFIG}/server.pid"
URL="http://127.0.0.1:8080"
RELEASE="https://github.com/ggml-org/llama.cpp/releases/download/b12180"

dialog() {
    osascript -e "display dialog $(printf '%q' "$1") buttons {\"OK\"} default button \"OK\" with title \"AnnoTune Loader\"" >/dev/null
}

health_ok() {
    curl -sf --max-time 2 "${URL}/health" 2>/dev/null | grep -q '"status":"ok"'
}

choose_file() {
    osascript -e "POSIX path of (choose file with prompt $(printf '%q' "$1"))" 2>/dev/null || true
}

stop_owned() {
    if [[ ! -f "$PIDFILE" ]]; then
        return
    fi
    local pid
    pid="$(cat "$PIDFILE" 2>/dev/null || true)"
    rm -f "$PIDFILE"
    if [[ -n "${pid}" ]] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        sleep 1
        kill -9 "$pid" 2>/dev/null || true
    fi
}

find_server() {
    local found
    found="$(find "$CACHE" "$ROOT" "$ROOT/bin" -name llama-server -type f 2>/dev/null | head -n 1 || true)"
    if [[ -n "$found" && -x "$found" ]]; then
        printf '%s\n' "$found"
        return 0
    fi
    if [[ -n "$found" ]]; then
        chmod +x "$found" 2>/dev/null || true
        printf '%s\n' "$found"
        return 0
    fi
    return 1
}

asset_name() {
    local machine
    machine="$(uname -m)"
    if [[ "$machine" == "arm64" ]]; then
        printf '%s\n' "llama-b12180-bin-macos-arm64.tar.gz"
    else
        printf '%s\n' "llama-b12180-bin-macos-x64.tar.gz"
    fi
}

ensure_server() {
    if find_server >/dev/null; then
        return 0
    fi
    local name archive
    name="$(asset_name)"
    archive="${CACHE}/${name}"
    dialog "Downloading the model server. This happens once and can take a few minutes."
    echo "Downloading ${name}"
    curl -L --fail --retry 3 --retry-delay 2 --output "${archive}.partial" "${RELEASE}/${name}" || return 1
    mv "${archive}.partial" "$archive"
    tar -xzf "$archive" -C "$CACHE" || return 1
    find_server >/dev/null
}

if health_ok; then
    dialog "A model server is already running at ${URL}. This loader will not start another. AnnoTuneAgent can use that address."
    exit 0
fi

BASE=""
NAME=""
ML=""
if [[ -f "$PATHS" ]]; then
    BASE="$(sed -n '1p' "$PATHS")"
    NAME="$(sed -n '2p' "$PATHS")"
    ML="$(sed -n '3p' "$PATHS")"
fi

if [[ -f "$BASE" && -f "$NAME" && -f "$ML" ]]; then
    CHOICE="$(osascript -e "button returned of (display dialog \"Use the weight files you chose last time?\" buttons {\"Choose again\", \"Start\"} default button \"Start\" with title \"AnnoTune Loader\")" 2>/dev/null || true)"
    if [[ "$CHOICE" != "Start" ]]; then
        BASE=""
        NAME=""
        ML=""
    fi
else
    BASE=""
    NAME=""
    ML=""
fi

if [[ -z "$BASE" ]]; then
    BASE="$(choose_file "Base model (.gguf)")"
    NAME="$(choose_file "Name adapter (.gguf)")"
    ML="$(choose_file "ML adapter (.gguf)")"
fi
BASE="${BASE%$'\r'}"
NAME="${NAME%$'\r'}"
ML="${ML%$'\r'}"

if [[ ! -f "$BASE" || ! -f "$NAME" || ! -f "$ML" ]]; then
    dialog "Choose the base model, the name adapter, and the ML adapter."
    exit 1
fi

if ! ensure_server; then
    dialog "Could not download llama-server."
    exit 1
fi
BIN="$(find_server)"

printf '%s\n%s\n%s\n' "$BASE" "$NAME" "$ML" >"$PATHS"
: >"$LOG"
: >"$ERR"
BINDIR="$(dirname "$BIN")"
export DYLD_LIBRARY_PATH="${BINDIR}:${CACHE}${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
nohup "$BIN" \
    -m "$BASE" \
    --lora "${NAME},${ML}" \
    --lora-init-without-apply \
    -c 8192 \
    -ngl 99 \
    -np 1 \
    --no-cont-batching \
    --no-cache-prompt \
    --slot-prompt-similarity 0 \
    --no-jinja \
    --host 127.0.0.1 \
    --port 8080 >>"$LOG" 2>"$ERR" &
echo $! >"$PIDFILE"

ready=0
for _ in $(seq 1 180); do
    if ! kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        break
    fi
    if health_ok; then
        ready=1
        break
    fi
    sleep 1
done

if [[ "$ready" -ne 1 ]]; then
    stop_owned
    tail="$(tail -n 12 "$ERR" "$LOG" 2>/dev/null || true)"
    dialog "The server did not become ready.

${tail}"
    exit 1
fi

osascript -e "display dialog \"Ready at ${URL}. Leave this window open while you annotate. Press Stop when you are finished.\" buttons {\"Stop\"} default button \"Stop\" with title \"AnnoTune Loader\"" >/dev/null || true
stop_owned
dialog "Stopped."
