FROM ubuntu:22.04

# Install dependencies.
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    gcc git wget make libncurses-dev flex bison gperf libglib2.0-dev

# Copy the moddable repo.
ENV MODDABLE /var/lib/moddable
ENV PATH=${PATH}:${MODDABLE}/build/bin/lin/release
RUN mkdir ${MODDABLE}  
WORKDIR /var/lib/moddable
COPY . .

# Build moddable.
WORKDIR ${MODDABLE}/build/makefiles/lin
RUN make