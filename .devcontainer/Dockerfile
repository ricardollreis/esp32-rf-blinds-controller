# Development container for RFBlinds: ESP32 firmware build (PlatformIO and
# arduino-cli), host-side tests (g++, numpy) and the serial tool (pyserial).
FROM mcr.microsoft.com/devcontainers/python:1-3.12-bookworm

ARG ARDUINO_CLI_VERSION=1.1.1
ARG TARGETARCH

# Native tools: g++ for the host tests, udev rules/libusb for flashing.
# The base image ships a Yarn apt repo whose signing key has expired, which
# makes apt-get update fail; Yarn is not needed here, so drop that repo.
RUN rm -f /etc/apt/sources.list.d/yarn.list \
 && apt-get update \
 && export DEBIAN_FRONTEND=noninteractive \
 && apt-get install -y --no-install-recommends \
      build-essential \
      libusb-1.0-0 \
      udev \
 && rm -rf /var/lib/apt/lists/*

# Python tools: PlatformIO (firmware build), numpy (decoder/tests), pyserial
# (src/tools/blinds_serial.py).
RUN pip install --no-cache-dir platformio numpy pyserial

# arduino-cli, pinned release from GitHub.
RUN case "${TARGETARCH:-amd64}" in \
      amd64) arch=64bit ;; \
      arm64) arch=ARM64 ;; \
      *) echo "unsupported architecture: $TARGETARCH" >&2; exit 1 ;; \
    esac \
 && curl -fsSL "https://github.com/arduino/arduino-cli/releases/download/v${ARDUINO_CLI_VERSION}/arduino-cli_${ARDUINO_CLI_VERSION}_Linux_${arch}.tar.gz" \
    | tar -xz -C /usr/local/bin arduino-cli \
 && arduino-cli version

# Serial port access for flashing, and toolchain folders owned by the user so
# the named volumes mounted there are writable.
RUN usermod -aG dialout vscode \
 && mkdir -p /home/vscode/.platformio /home/vscode/.arduino15 \
 && chown -R vscode:vscode /home/vscode/.platformio /home/vscode/.arduino15
