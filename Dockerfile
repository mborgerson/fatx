FROM ubuntu:20.04
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -qy \
        build-essential pkg-config libfuse-dev cmake
COPY . /usr/src/fatx
WORKDIR /usr/src/fatx
RUN mkdir build \
 && cd build \
 && cmake .. \
 && make DESTDIR=/fatx install

FROM ubuntu:20.04
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -qy fuse
COPY --from=0 /fatx /fatx
RUN cp -ruT /fatx / && rm -rf /fatx
