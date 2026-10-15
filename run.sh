#!/bin/bash

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

echo "[Launcher] : Starting Server on port 5432..."
./OpenNoteServer 5432 &
SERVER_PID=$!

# Give the server some time to bind
sleep 0.5

echo "[Launcher] : Starting Client..."
# Run the client
./OpenNoteClient 127.0.0.1

# Once the client window is closed, clean up the background server
echo "[Launcher] : Client closed. Shutting down server (PID: $SERVER_PID)..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo "[Launcher] : All systems shut down."
