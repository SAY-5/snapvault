# Multi-stage build producing both snapvault binaries in one small image.

# Stage 1: build the C++ engine with CMake.
FROM debian:bookworm-slim AS cpp-build
RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake g++ make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY core/ core/
RUN cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build core/build \
    && ctest --test-dir core/build --output-on-failure

# Stage 2: build the Go distribution layer.
FROM golang:1.22-bookworm AS go-build
WORKDIR /src
COPY go.mod ./
COPY cmd/ cmd/
COPY internal/ internal/
RUN CGO_ENABLED=0 go build -o /out/snapvault ./cmd/snapvault

# Stage 3: minimal runtime with both binaries and the demo assets.
FROM debian:bookworm-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends bash coreutils \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=cpp-build /src/core/build/svcore /usr/local/bin/svcore
COPY --from=go-build /out/snapvault /usr/local/bin/snapvault
COPY scripts/ scripts/
COPY Makefile FORMAT.md ARCHITECTURE.md README.md ./
ENTRYPOINT ["/bin/bash"]
