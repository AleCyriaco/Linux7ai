# THK - LLM Command Assistant
# Build and test in Docker
#
# Build:  docker build -t thk -f tools/thk/Dockerfile .
# Run:    docker run -it thk
# Custom: docker run -it -e THK_MODEL=gemma3:4b thk
#
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    curl \
    zstd \
    && rm -rf /var/lib/apt/lists/*

# Copy source
WORKDIR /src
COPY include/uapi/misc/thk.h include/uapi/misc/thk.h
COPY tools/thk/ tools/thk/

# Build userspace tools (clean first in case of cross-platform .o files)
RUN make -C tools/thk clean && make -C tools/thk

# Install Ollama
RUN curl -fsSL https://ollama.com/install.sh | sh

# Setup directories and config
RUN mkdir -p /etc/thk /run/thk && \
    cp tools/thk/config/thk.conf.example /etc/thk/thk.conf

# Startup script
COPY tools/thk/docker-start.sh /start.sh
RUN chmod +x /start.sh

ENV PATH="/src/tools/thk:${PATH}"

ENTRYPOINT ["/start.sh"]
