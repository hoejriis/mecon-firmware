# Build and test container for mecon-firmware.
# Mirrors the CI runner's toolchain (Linux, Python 3.13, a pinned PlatformIO),
# so a local build and a CI build use the same tools.
FROM python:3.13-slim-bookworm

ARG PLATFORMIO_VERSION=6.2.0

# libssl-dev: the simulated board's TLS (MQTT controller). mosquitto, mosquitto-clients, openssl: the real broker, its
# clients and the test certificates for the MQTT tests (docs/MQTT.md). The broker is not started by the image.
RUN apt-get update \
 && apt-get install -y --no-install-recommends git build-essential jq curl ca-certificates \
      libssl-dev openssl mosquitto mosquitto-clients \
 && rm -rf /var/lib/apt/lists/* \
 && (systemctl disable mosquitto 2>/dev/null || true)

RUN pip install --no-cache-dir "platformio==${PLATFORMIO_VERSION}" pytest PyYAML

RUN useradd -m -u 1000 dev \
 && mkdir -p /home/dev/.platformio \
 && chown dev:dev /home/dev/.platformio
USER dev
WORKDIR /workspaces/mecon-firmware
ENV PLATFORMIO_CORE_DIR=/home/dev/.platformio
