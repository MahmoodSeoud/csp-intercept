# Linux build environment for the csp-intercept front-ends (proxy + monitor APM).
# libcsp/CSH target Linux; the front-ends do not build on macOS host. This image
# gives a fast local Linux build/test loop on any dev machine (see scripts/lbuild)
# and mirrors the CI environment (ubuntu + libzmq3-dev).
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        meson \
        ninja-build \
        build-essential \
        libzmq3-dev \
        libbsd-dev \
        pkg-config \
        git \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
