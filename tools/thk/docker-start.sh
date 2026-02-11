#!/bin/bash
set -e

echo "=== THK - LLM Command Assistant ==="
echo ""

# Start Ollama
echo "[1/3] Starting Ollama..."
ollama serve &
sleep 3

# Pull model (use small model for Docker)
MODEL="${THK_MODEL:-tinyllama}"
echo "[2/3] Pulling model: $MODEL ..."
ollama pull "$MODEL"

# Update config with model
sed -i "s/^model=.*/model=$MODEL/" /etc/thk/thk.conf

# Start thkd
echo "[3/3] Starting thkd daemon..."
/src/tools/thk/thkd -v -f &
sleep 1

echo ""
echo "=== THK ready! ==="
echo "Usage: thk \"your question here\""
echo ""

exec bash
