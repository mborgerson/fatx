FROM ubuntu:17.10
RUN apt-get update && apt-get install -y build-essential pkg-config libfuse-dev cmake git
RUN cd /usr/src && \
    git clone https://github.com/mborgerson/fatx && \
    cd fatx && mkdir build && cd build && \
    cmake .. && make && make install
