# Build and test container for mecon-firmware.
# Mirrors the CI runner's toolchain (Linux, Python 3.13, a pinned PlatformIO),
# so a local build and a CI build use the same tools.
FROM python:3.13-slim-bookworm

ARG PLATFORMIO_VERSION=6.2.0

RUN apt-get update \
 && apt-get install -y --no-install-recommends git build-essential jq curl ca-certificates \
 && rm -rf /var/lib/apt/lists/*

RUN pip install --no-cache-dir "platformio==${PLATFORMIO_VERSION}" pytest

RUN useradd -m -u 1000 dev \
 && mkdir -p /home/dev/.platformio \
 && chown dev:dev /home/dev/.platformio
USER dev
WORKDIR /workspaces/mecon-firmware
ENV PLATFORMIO_CORE_DIR=/home/dev/.platformio
