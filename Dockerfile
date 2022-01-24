FROM ubuntu:20.04
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -qy \
    build-essential pkg-config libfuse-dev cmake
COPY . /usr/src/fatx
WORKDIR /usr/src/fatx
RUN mkdir build \
 && cd build \
 && cmake .. \
 && make install
