# Local webOS build environment, the same toolchain CI uses.
#   docker build -t marigold-sdk tools/
#   docker run --rm -v "$PWD":/work marigold-sdk bash tools/build-webos.sh
FROM debian:bookworm-slim
RUN apt-get update \
  && apt-get install -y --no-install-recommends ca-certificates curl cmake ninja-build \
     python3 binutils file xz-utils \
  && rm -rf /var/lib/apt/lists/*
COPY sdk.conf fetch-sdk.sh /tmp/sdk/
RUN bash /tmp/sdk/fetch-sdk.sh /opt && rm -rf /tmp/sdk
ENV MARIGOLD_SDK=/opt/arm-webos-linux-gnueabi_sdk-buildroot
WORKDIR /work
