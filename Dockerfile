FROM alpine:3.23 AS builder

# TODO: Add croaring-dev once available in Alpine Linux.
# https://gitlab.alpinelinux.org/alpine/aports/-/merge_requests/87769
RUN apk add --no-cache  \
    clang               \
    cmake               \
    git                 \
    linux-headers       \
    make                \
    python3-dev         \
                        \
    bzip2-dev           \
    catch2-3            \
    capnproto-dev       \
    cxxopts-dev         \
    expat-dev           \
    libosmium-dev       \
    lmdb-dev            \
    nlohmann-json       \
    openssl-dev         \
    protozero-dev       \
    zlib-dev

WORKDIR /usr/src/osmexpress
COPY . /usr/src/osmexpress

RUN cmake -DCMAKE_BUILD_TYPE=Release .
RUN make -j16 && ./osmxTest && make install


FROM alpine:3.23

# cxxopts, libosmium, nlohmann-json and protozero are header-only
# C++ libraries; catch2 is only used for testing. We do not need
# them in the production container.
RUN apk add --no-cache  \
    libbz2              \
    libcrypto3          \
    capnproto           \
    libexpat            \
    libssl3             \
    lmdb                \
    zlib

COPY --from=builder /usr/local/bin/osmx /usr/local/bin/osmx
ENTRYPOINT [ "/usr/local/bin/osmx" ]
