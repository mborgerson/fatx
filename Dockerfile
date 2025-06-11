FROM ubuntu:24.04 as builder
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -qy \
        build-essential pkg-config libfuse-dev cmake

COPY libfatx /usr/src/fatx/libfatx
COPY fatxfs /usr/src/fatx/fatxfs

WORKDIR /usr/src/fatx

# Build libfatx
RUN cmake -Blibfatx_build -Slibfatx \
 && cmake --build libfatx_build --target install

# Build fatxfs
RUN cmake -Bfatxfs -Sfatxfs -DCMAKE_INSTALL_PREFIX=/fatx \
 && cmake --build fatxfs --target install

FROM ubuntu:24.04
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -qy fuse
COPY --from=builder /fatx /usr/local

