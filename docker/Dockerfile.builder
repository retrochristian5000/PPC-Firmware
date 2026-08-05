FROM debian:12.5@sha256:a92ed51e0996d8e9de041ca05ce623d2c491444df6a535a566dabd5cb8336946

ARG FCODE_UTILS_COMMIT=6478df0f5b8bb6bf3f8654482cc2aa84264e3805

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates git make xsltproc zip libc6-dev-i386 gcc \
        gcc-multilib-powerpc-linux-gnu gcc-multilib-sparc64-linux-gnu && \
    git clone --filter=blob:none --no-checkout \
        https://github.com/retrochristian5000/fcode-utils.git \
        /tmp/fcode-utils && \
    git -C /tmp/fcode-utils fetch --depth=1 origin "$FCODE_UTILS_COMMIT" && \
    git -C /tmp/fcode-utils checkout --detach --force FETCH_HEAD && \
    sed -i \
        's/tic_param_t dummy_param;/tic_param_t dummy_param = { 0 };/' \
        /tmp/fcode-utils/toke/tokzesc.c && \
    grep -q 'tic_param_t dummy_param = { 0 };' \
        /tmp/fcode-utils/toke/tokzesc.c && \
    make -C /tmp/fcode-utils HOSTCC=gcc HOSTSTRIP=true && \
    make -C /tmp/fcode-utils DESTDIR=/usr install && \
    rm -rf /tmp/fcode-utils /var/lib/apt/lists/*
