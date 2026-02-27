FROM moddable

# Install deps
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    ca-certificates xz-utils cmake g++

# Handle Emscripten
WORKDIR /var/lib/emsdk
RUN git clone https://github.com/emscripten-core/emsdk.git .
RUN ./emsdk install latest && \
    ./emsdk activate latest
ENV PATH=$PATH:/var/lib/emsdk:/var/lib/emsdk/node/22.16.0_64bit/bin:/var/lib/emsdk/upstream/emscripten

# Handle Binaryen
WORKDIR /var/lib/binaryen
RUN git clone --recursive https://github.com/WebAssembly/binaryen.git .
RUN cmake . && make
ENV PATH=$PATH:/var/lib/binaryen/bin