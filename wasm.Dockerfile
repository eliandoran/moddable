ARG BASE_IMAGE=moddable
FROM ${BASE_IMAGE}

# Install deps
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    ca-certificates xz-utils cmake g++ lbzip2

# Handle Emscripten
WORKDIR /var/lib/emsdk
RUN git clone https://github.com/emscripten-core/emsdk.git .
RUN git reset --hard 476a14d60d0d25ff5a1bfee18af73a4b9bfbd385
RUN ./emsdk install latest && \
    ./emsdk activate latest
ENV PATH=$PATH:/var/lib/emsdk:/var/lib/emsdk/node/22.16.0_64bit/bin:/var/lib/emsdk/upstream/emscripten

# Handle Binaryen
WORKDIR /var/lib/binaryen
RUN git clone --recursive https://github.com/WebAssembly/binaryen.git .
RUN cmake . && make
ENV PATH=$PATH:/var/lib/binaryen/bin

# Build Moddable WASM tools
WORKDIR ${MODDABLE}/build/makefiles/wasm
RUN make

COPY ./entrypoint-wasm.sh .
RUN chmod +x ./entrypoint-wasm.sh
ENTRYPOINT [ "./entrypoint-wasm.sh" ]