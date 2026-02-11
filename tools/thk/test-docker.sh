#!/bin/bash
# THK - Automated validation test script for Docker
# Usage: ./test-docker.sh
#
# Builds the Docker image and runs all validation tests.

set -e
cd "$(dirname "$0")/../.."

PASS=0
FAIL=0
TOTAL=0

pass() { echo "  PASS: $1"; PASS=$((PASS+1)); TOTAL=$((TOTAL+1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL+1)); TOTAL=$((TOTAL+1)); }
header() { echo ""; echo "=== $1 ==="; }

header "Building Docker image"
docker build -t thk-test -f tools/thk/Dockerfile . -q
echo "  Image built: thk-test"

header "Running validation tests inside Docker"
RESULT=$(docker run --rm --entrypoint bash thk-test -c '
# Start services
ollama serve &>/dev/null &
sleep 3
ollama pull tinyllama 2>&1 | tail -1
sed -i "s/^model=.*/model=tinyllama/" /etc/thk/thk.conf
thkd -f -c /etc/thk/thk.conf &>/dev/null &
sleep 1

echo "TEST_VERSION_START"
thk --version
echo "TEST_VERSION_END"

echo "TEST_HELP_START"
thk --help
echo "TEST_HELP_END"

echo "TEST_THKD_SOCKET_START"
ls -la /run/thk/thk.sock 2>&1
echo "TEST_THKD_SOCKET_END"

echo "TEST_QUERY1_START"
thk "list files in current directory"
echo "TEST_QUERY1_END"

echo "TEST_QUERY2_START"
thk "como verificar espaco em disco?"
echo "TEST_QUERY2_END"

echo "TEST_BINARIES_START"
ls -la /src/tools/thk/thk /src/tools/thk/thkd
echo "TEST_BINARIES_END"

echo "TEST_CONFIG_START"
cat /etc/thk/thk.conf
echo "TEST_CONFIG_END"
' 2>&1)

# Validate results
header "Test 1: CLI version"
VERSION=$(echo "$RESULT" | sed -n '/TEST_VERSION_START/,/TEST_VERSION_END/p')
if echo "$VERSION" | grep -q "thk CLI version 1.0.0"; then
    pass "thk --version reports 1.0.0"
else
    fail "thk --version unexpected output"
    echo "$VERSION"
fi

header "Test 2: CLI help"
HELP=$(echo "$RESULT" | sed -n '/TEST_HELP_START/,/TEST_HELP_END/p')
if echo "$HELP" | grep -q "\-\-version" && echo "$HELP" | grep -q "\-\-status"; then
    pass "thk --help shows all options"
else
    fail "thk --help missing options"
fi

header "Test 3: Daemon socket"
SOCKET=$(echo "$RESULT" | sed -n '/TEST_THKD_SOCKET_START/,/TEST_THKD_SOCKET_END/p')
if echo "$SOCKET" | grep -q "thk.sock"; then
    pass "thkd created Unix socket at /run/thk/thk.sock"
else
    fail "thkd socket not found"
    echo "$SOCKET"
fi

header "Test 4: LLM query (English)"
Q1=$(echo "$RESULT" | sed -n '/TEST_QUERY1_START/,/TEST_QUERY1_END/p')
if echo "$Q1" | grep -q "THK:"; then
    pass "thk received LLM response (English)"
else
    fail "thk got no response"
    echo "$Q1"
fi

header "Test 5: LLM query (Portuguese)"
Q2=$(echo "$RESULT" | sed -n '/TEST_QUERY2_START/,/TEST_QUERY2_END/p')
if echo "$Q2" | grep -q "THK:"; then
    pass "thk received LLM response (Portuguese)"
else
    fail "thk got no response"
    echo "$Q2"
fi

header "Test 6: Binaries are Linux ELF"
BINS=$(echo "$RESULT" | sed -n '/TEST_BINARIES_START/,/TEST_BINARIES_END/p')
if echo "$BINS" | grep -q "/src/tools/thk/thk" && echo "$BINS" | grep -q "/src/tools/thk/thkd"; then
    pass "thk and thkd binaries exist"
else
    fail "binaries not found"
    echo "$BINS"
fi

header "Test 7: Config file installed"
CFG=$(echo "$RESULT" | sed -n '/TEST_CONFIG_START/,/TEST_CONFIG_END/p')
if echo "$CFG" | grep -q "backend=" && echo "$CFG" | grep -q "socket_path="; then
    pass "/etc/thk/thk.conf installed with correct keys"
else
    fail "config file incomplete"
fi

# Summary
echo ""
echo "================================"
echo "  Results: $PASS passed, $FAIL failed (out of $TOTAL)"
echo "================================"

if [ $FAIL -gt 0 ]; then
    exit 1
fi
