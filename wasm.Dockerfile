FROM moddable

# Install deps
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    ca-certificates xz-utils

# Handle Emscripten
WORKDIR /var/lib/emsdk
RUN git clone https://github.com/emscripten-core/emsdk.git .
RUN ./emsdk install latest && \
    ./emsdk activate latest