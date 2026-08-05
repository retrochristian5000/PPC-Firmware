FROM ghcr.io/openbios/fcode-utils:master AS cross

ARG FCODE_UTILS_COMMIT=6e563ee54aa9f60e538d90eedaa012ae77610344

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates git make xsltproc zip libc6-dev-i386 gcc \
        gcc-multilib-powerpc-linux-gnu gcc-multilib-sparc64-linux-gnu && \
    git clone --filter=blob:none --no-checkout \
        https://github.com/openbios/fcode-utils.git /tmp/fcode-utils && \
    git -C /tmp/fcode-utils fetch --depth=1 origin "$FCODE_UTILS_COMMIT" && \
    git -C /tmp/fcode-utils checkout --detach --force FETCH_HEAD && \
    sed -i \
        's/tic_param_t dummy_param;/tic_param_t dummy_param = { 0 };/' \
        /tmp/fcode-utils/toke/tokzesc.c && \
    grep -q 'tic_param_t dummy_param = { 0 };' \
        /tmp/fcode-utils/toke/tokzesc.c && \
    make -C /tmp/fcode-utils/toke STRIP=true && \
    install -m 0755 /tmp/fcode-utils/toke/toke /usr/bin/toke && \
    rm -rf /tmp/fcode-utils /var/lib/apt/lists/*
