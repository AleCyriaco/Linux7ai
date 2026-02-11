.. SPDX-License-Identifier: GPL-2.0

===================================
THK - LLM Command Assistant for Linux
===================================

:Author: Linux7ai

Introduction
============

THK (Think) is a kernel-assisted LLM command assistant that provides
intelligent command suggestions through a hybrid architecture combining
kernel-space security enforcement with userspace LLM inference.

The system consists of three components:

1. **Kernel module** (``/dev/thk``): Provides immutable audit logging,
   command validation against a blocklist, per-UID rate limiting, and
   statistics via sysfs.

2. **Daemon** (``thkd``): Manages LLM backend connections (Ollama,
   OpenAI, Anthropic, llama.cpp), handles client requests over a Unix
   socket, and formats LLM responses into structured command steps.

3. **CLI** (``thk``): User-facing tool that sends natural language
   queries to the daemon and presents interactive command execution.

Architecture
============

::

    thk CLI --Unix socket--> thkd daemon --HTTP--> LLM Backend
    thk CLI --ioctl-------> /dev/thk (kernel) --> audit + validation

Device Interface
================

The kernel module creates a ``/dev/thk`` character device with the
following ioctl interface (magic number ``0xBB``):

.. flat-table:: THK ioctl commands
   :header-rows: 1

   * - Ioctl
     - Direction
     - Description

   * - ``THK_IOC_EXEC_VALIDATE``
     - Write
     - Validate a command against blocklist and rate limits

   * - ``THK_IOC_EXEC_STATUS``
     - Read
     - Get the result of the last validation

   * - ``THK_IOC_GET_STATS``
     - Read
     - Get module statistics (requests, blocked, allowed)

   * - ``THK_IOC_GET_CONFIG``
     - Read
     - Get current module configuration

   * - ``THK_IOC_SET_CONFIG``
     - Write
     - Update module configuration (requires CAP_SYS_ADMIN)

   * - ``THK_IOC_VERSION``
     - Read
     - Get module version number

sysfs Interface
===============

The module exposes the following attributes under
``/sys/devices/virtual/misc/thk/``:

``version`` (read-only)
    Module version string.

``stats`` (read-only)
    Current statistics: request counts, blocked counts, uptime.

``audit_enabled`` (read-write)
    Enable/disable audit logging (0 or 1).

``rate_limit`` (read-write)
    Maximum requests per minute per UID (0 = unlimited).

``blocklist`` (read-only)
    Current list of blocked command patterns.

Security
========

- The device node is created with mode 0666, as it only validates
  and audits commands but never executes them.
- Command execution happens in userspace, with each command first
  validated through the kernel module.
- All validations are logged to the kernel audit subsystem when
  audit is enabled, providing an immutable execution trail.
- A built-in blocklist prevents known dangerous commands (``rm -rf /``,
  fork bombs, raw device writes, etc.).
- Per-UID rate limiting prevents abuse.
- ``THK_IOC_SET_CONFIG`` requires ``CAP_SYS_ADMIN``.
- The daemon socket uses Unix permissions (root:thk 0770) and
  verifies client credentials via ``SO_PEERCRED``.

Configuration
=============

The daemon reads ``/etc/thk/thk.conf`` on startup::

    backend=ollama
    endpoint=http://localhost:11434
    model=llama3.2
    max_tokens=2048
    temperature=0.3
    socket_path=/run/thk/thk.sock
    audit_enabled=1
    rate_limit=10

See ``tools/thk/config/thk.conf.example`` for all options.

Usage
=====

::

    # Load kernel module
    modprobe thk

    # Start daemon
    thkd -c /etc/thk/thk.conf

    # Query
    thk "how to check disk usage?"

    # Check status
    thk --status

Building
========

Kernel module::

    make M=drivers/misc/thk

Userspace tools::

    make -C tools/thk

Selftests::

    make -C tools/testing/selftests/drivers/thk run_tests
