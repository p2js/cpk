# Dockerfile for building an aarch64 static binary of cpk using musl libc
# Requires the following command as a one-time setup for use from x86_64:
# docker run --privileged --rm tonistiigi/binfmt --install arm64

FROM --platform=linux/arm64 alpine:latest AS build

RUN apk add --no-cache build-base musl-dev

WORKDIR /src

COPY . .
COPY cpk.tar /src/

RUN rm -rf .cpk
RUN tar -xf cpk.tar && rm cpk.tar

RUN gcc -static -O3 -s -I.cpk src/main.c -o cpk

FROM scratch AS export
COPY --from=build /src/cpk /
CMD ["/cpk"]