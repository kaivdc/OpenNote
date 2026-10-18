#!/bin/bash

if [[ $# -gt 1 ]]; then
    echo "Usage: ./run.sh [port]"
    exit 1
fi

SERVER_PORT="${1:-5432}"
if [[ ! "$SERVER_PORT" =~ ^[0-9]{1,5}$ ]]; then
    echo "[ERROR] : Port must be a number between 1 and 65535."
    exit 1
fi

SERVER_PORT_NUMBER=$((10#$SERVER_PORT))
if (( SERVER_PORT_NUMBER < 1 || SERVER_PORT_NUMBER > 65535 )); then
    echo "[ERROR] : Port must be a number between 1 and 65535."
    exit 1
fi
SERVER_PORT="$SERVER_PORT_NUMBER"

# Clean previous builds
make clean

# Determine the number of CPU cores across mac/Linux/WSL
if [[ "$OSTYPE" == "darwin"* ]]; then
    NUM_CORES=$(sysctl -n hw.ncpu)
else
    NUM_CORES=$(nproc)
fi

# Build the project using all available cores
echo "[Launcher] : Building project..."
if ! make -j"$NUM_CORES"; then
    echo "[ERROR] : Build failed. Aborting launch."
    exit 1
fi

echo "[Launcher] : Starting Server on port $SERVER_PORT..."
./OpenNoteServer "$SERVER_PORT" &
SERVER_PID=$!

# Give the server some time to bind
sleep 0.5

echo "[Launcher] : Starting Client..."
# Run the client
./OpenNoteClient 127.0.0.1 "$SERVER_PORT"

# Once the client window is closed, clean up the background server
echo "[Launcher] : Client closed. Shutting down server (PID: $SERVER_PID)..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo "[Launcher] : All systems shut down."
