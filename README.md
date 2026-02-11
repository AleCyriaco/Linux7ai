# Linux7ai - THK (Think) LLM Command Assistant

A hybrid kernel/userspace system that integrates LLM-powered command assistance directly into the Linux kernel. Ask questions in natural language, get executable shell commands back — validated by the kernel before execution.

```
$ thk "como verificar uso de memoria por processo?"

THK: Verificar uso de memoria por processo:

  1. Visao geral da memoria do sistema
     $ free -h

  2. Top 20 processos por uso de memoria
     $ ps aux --sort=-%mem | head -20

  3. Detalhes de um processo especifico (substitua PID)
     $ cat /proc/<PID>/status | grep -i mem

  [E]xecutar todos  [S]elecionar  [C]ancelar
```

## Architecture

```
thk CLI ──Unix socket──> thkd daemon ──HTTP──> LLM (Ollama/OpenAI/Anthropic/llama.cpp)
thk CLI ──ioctl────────> /dev/thk (kernel) ──> audit log + validation + stats
```

| Component | Role |
|-----------|------|
| **Kernel module** (`/dev/thk`) | Immutable audit logging, command blocklist, per-UID rate limiting |
| **Daemon** (`thkd`) | LLM inference, backend management, Unix socket server |
| **CLI** (`thk`) | User interface, colored output, interactive execution |

## Features

- **4 LLM backends**: Ollama, OpenAI, Anthropic, llama.cpp
- **Kernel-level security**: Commands validated against blocklist before execution
- **Immutable audit trail**: All commands logged via kernel audit subsystem
- **Rate limiting**: Per-UID request throttling (configurable)
- **Dangerous command blocking**: `rm -rf /`, fork bombs, reverse shells, raw device writes
- **Interactive mode**: Execute all, select specific steps, or cancel
- **Colored terminal output** with step numbering and danger markers
- **sysfs interface**: Runtime stats and config at `/sys/devices/virtual/misc/thk/`

## Quick Start (Docker)

```bash
# Build
docker build -t thk -f tools/thk/Dockerfile .

# Run interactively
docker run -it thk

# Inside the container
thk "how to find large files?"
thk "como listar portas abertas?"
```

Use a different model:
```bash
docker run -it -e THK_MODEL=gemma3:4b thk
```

## Quick Start (Local - macOS/Linux)

```bash
# Build userspace tools
make -C tools/thk

# Start Ollama
ollama serve &
ollama pull llama3.2

# Configure
mkdir -p /tmp/thk
cat > /tmp/thk/thk.conf << 'EOF'
backend=ollama
endpoint=http://localhost:11434
model=llama3.2
socket_path=/tmp/thk/thk.sock
EOF

# Start daemon
tools/thk/thkd -v -f -c /tmp/thk/thk.conf &

# Query
tools/thk/thk --socket /tmp/thk/thk.sock "how to check disk usage?"
```

## Building the Kernel Module

Requires a Linux system with kernel headers:

```bash
# Enable in config
make defconfig
scripts/config --module CONFIG_THK
scripts/config --enable CONFIG_THK_AUDIT

# Build module only
make M=drivers/misc/thk

# Load
sudo insmod drivers/misc/thk/thk.ko
dmesg | grep thk
# thk: LLM Command Assistant v1.0.0 loaded

# Verify
ls -la /dev/thk
cat /sys/devices/virtual/misc/thk/version
cat /sys/devices/virtual/misc/thk/stats
cat /sys/devices/virtual/misc/thk/blocklist
```

## Full Installation (Linux)

```bash
# Build everything
make M=drivers/misc/thk
make -C tools/thk

# Install
sudo insmod drivers/misc/thk/thk.ko
sudo make -C tools/thk install

# Configure
sudo vim /etc/thk/thk.conf

# Start daemon
sudo thkd &

# Use
thk "your question here"
```

## Configuration

`/etc/thk/thk.conf`:

```ini
backend=ollama              # ollama | openai | anthropic | llamacpp
endpoint=http://localhost:11434
model=llama3.2
# api_key=sk-...            # required for openai/anthropic
max_tokens=2048
temperature=0.3
socket_path=/run/thk/thk.sock
audit_enabled=1
rate_limit=10               # requests per minute per user
```

## IOCTL Interface

The kernel module exposes `/dev/thk` with ioctl magic `0xBB`:

| Ioctl | Code | Direction | Description |
|-------|------|-----------|-------------|
| `THK_IOC_EXEC_VALIDATE` | 0x01 | Write | Validate command against blocklist |
| `THK_IOC_EXEC_STATUS` | 0x02 | Read | Get last validation result |
| `THK_IOC_GET_STATS` | 0x03 | Read | Module statistics |
| `THK_IOC_GET_CONFIG` | 0x04 | Read | Current configuration |
| `THK_IOC_SET_CONFIG` | 0x05 | Write | Update config (CAP_SYS_ADMIN) |
| `THK_IOC_VERSION` | 0x06 | Read | Module version |

## Testing

### Automated (Docker)
```bash
./tools/thk/test-docker.sh
```

### Kernel selftests (Linux VM)
```bash
make -C tools/testing/selftests/drivers/thk run_tests
```

9 test cases: version, stats, config, safe/blocked/fork-bomb/empty command validation, stats increment, invalid ioctl.

## Security Model

| Layer | Protection |
|-------|-----------|
| Kernel (`/dev/thk`) | Blocklist validation, audit logging, rate limiting |
| Daemon (`thkd`) | Unix socket permissions (root:thk 0770), `SO_PEERCRED` |
| CLI (`thk`) | Validates every command through `/dev/thk` before `system()` |

**Blocked patterns include**: `rm -rf /`, fork bombs (`:(){ :\|:& };:`), `dd` to block devices, `mkfs` on mounted filesystems, hex-encoded shellcode, reverse shells (`nc -e`, `/dev/tcp/`), pipe-to-shell (`curl\|sh`).

## Project Structure

```
include/uapi/misc/thk.h          # UAPI: ioctls, structs, constants
drivers/misc/thk/
  ├── Kconfig                     # Build configuration
  ├── Makefile                    # Kernel build rules
  ├── thk.h                      # Internal module header
  ├── thk_main.c                 # misc_register, file_ops, ioctl dispatch
  ├── thk_exec.c                 # Validation, audit, blocklist
  └── thk_sysfs.c                # sysfs attributes
tools/thk/
  ├── Makefile                    # Userspace build
  ├── thk.c                      # CLI
  ├── thkd.c                     # Daemon
  ├── thk_llm.c                  # LLM backend abstraction
  ├── thk_config.c               # Config parser
  ├── thk_format.c               # Terminal formatting
  ├── Dockerfile                  # Container build
  ├── test-docker.sh              # Automated tests
  └── config/thk.conf.example    # Example configuration
tools/testing/selftests/drivers/thk/
  ├── thk_test.c                  # Kernel selftests
  ├── Makefile
  └── config
Documentation/misc-devices/thk.rst
```

## License

GPL-2.0 — Same as the Linux kernel.
