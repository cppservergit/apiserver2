# syntax=docker/dockerfile:1
ARG UBUNTU_RELEASE=24.04

# ==============================================================================
# STAGE 1: BUILDER (High-Performance Compilation)
# ==============================================================================
FROM ubuntu:$UBUNTU_RELEASE AS builder
ENV DEBIAN_FRONTEND=noninteractive
ENV TERM=xterm

# 1. Install Dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential g++-14 make ca-certificates wget git pkg-config \
    libssl-dev zlib1g-dev uuid-dev liboath-dev tzdata \
    netbase binutils libltdl-dev cmake

# ------------------------------------------------------------------------------
# GLOBAL OPTIMIZATION FLAGS
# -O2:        High optimization (Standard for production)
# -flto=auto: Parallel Link Time Optimization
# ------------------------------------------------------------------------------
ENV CFLAGS="-O2 -flto=auto" \
    CXXFLAGS="-O2 -flto=auto" \
    LDFLAGS="-O2 -flto=auto" \
    AR=gcc-ar \
    NM=gcc-nm \
    RANLIB=gcc-ranlib

# 2. COMPILE MINIMAL LIBCURL
WORKDIR /tmp/curl
RUN wget https://github.com/curl/curl/releases/download/curl-8_5_0/curl-8.5.0.tar.gz -O curl.tar.gz \
    && tar -xvf curl.tar.gz --strip-components=1 \
    && ./configure --prefix=/usr --with-ssl --with-zlib \
       --disable-dict --disable-file --disable-ftp --disable-gopher --disable-imap \
       --disable-ldap --disable-ldaps --disable-mqtt --disable-pop3 --disable-rtsp \
       --disable-smb --disable-smtp --disable-telnet --disable-tftp \
       --without-libssh2 --without-librtmp --without-libidn2 --without-nghttp2 \
       --without-brotli --without-zstd \
    && make -j$(nproc) && make install

# 3. COMPILE MINIMAL UNIXODBC
WORKDIR /tmp/unixodbc
RUN wget https://www.unixodbc.org/unixODBC-2.3.12.tar.gz -O unixodbc.tar.gz \
    && tar -xvf unixodbc.tar.gz --strip-components=1 \
    && ./configure --prefix=/usr --sysconfdir=/etc --disable-gui --disable-readline \
       --enable-iconv --with-iconv-char-enc=UTF8 --with-iconv-ucode-enc=UTF16LE \
    && make -j$(nproc) && make install

# 4. COMPILE MINIMAL FREETDS
WORKDIR /tmp/freetds
RUN wget https://www.freetds.org/files/stable/freetds-1.4.10.tar.gz -O freetds.tar.gz \
    && tar -xvf freetds.tar.gz --strip-components=1 \
    && ./configure --prefix=/usr --with-unixodbc=/usr --with-openssl=/usr \
       --enable-msdblib --disable-libiconv --disable-krb5 --disable-gssapi \
       --disable-apps --disable-server --disable-pool \
    && make -j$(nproc) && make install

# 5. COMPILE MINIMAL JSON-C (Optimized)
WORKDIR /tmp/json-c
RUN wget https://s3.amazonaws.com/json-c_releases/releases/json-c-0.17.tar.gz -O json-c.tar.gz \
    && tar -xvf json-c.tar.gz --strip-components=1 \
    && mkdir build && cd build \
    && cmake .. \
       -DCMAKE_INSTALL_PREFIX=/usr \
       -DBUILD_SHARED_LIBS=ON \
       -DBUILD_STATIC_LIBS=OFF \
       -DISABLE_WERROR=ON \
       -DENABLE_THREADING=ON \
       -DENABLE_RDRAND=ON \
    && make -j$(nproc) && make install

# 6. COMPILE APPLICATION
WORKDIR /src
COPY . .
RUN make release CXX=g++-14

# 7. STRIP BINARIES (Fix: Corrected json-c path)
# 'libjson-c' usually installs to x86_64-linux-gnu when built with CMake on Ubuntu
RUN strip --strip-unneeded /usr/lib/libtdsodbc.so \
    && strip --strip-unneeded /usr/lib/libodbc.so.2 \
    && strip --strip-unneeded /usr/lib/libodbcinst.so.2 \
    # We try both paths for json-c to be safe, or just the correct one
    && if [ -f /usr/lib/libjson-c.so.5 ]; then strip --strip-unneeded /usr/lib/libjson-c.so.5; \
       else strip --strip-unneeded /usr/lib/x86_64-linux-gnu/libjson-c.so.5; fi

# ==============================================================================
# STAGE 2: CHISELER
# ==============================================================================
FROM ubuntu:$UBUNTU_RELEASE AS chiseler
ARG UBUNTU_RELEASE
ARG TARGETARCH
ARG CHISEL_VERSION=v1.1.0
ENV DEBIAN_FRONTEND=noninteractive

# 1. Install Chisel
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*
ADD "https://github.com/canonical/chisel/releases/download/${CHISEL_VERSION}/chisel_${CHISEL_VERSION}_linux_${TARGETARCH}.tar.gz" chisel.tar.gz
RUN tar -xvf chisel.tar.gz -C /usr/bin/ && rm chisel.tar.gz

WORKDIR /rootfs

# 2. Cut Slices
RUN chisel cut --release ubuntu-$UBUNTU_RELEASE --root /rootfs \
    base-files_base base-files_release-info base-files_chisel ca-certificates_data \
    libgcc-s1_libs libc6_libs libstdc++6_libs libssl3t64_libs zlib1g_libs

# 3. Manual Harvest (Dependencies)
COPY --from=builder /etc/services /rootfs/etc/services
COPY --from=builder /etc/protocols /rootfs/etc/protocols
COPY --from=builder /etc/ssl/certs /rootfs/etc/ssl/certs
COPY --from=builder /usr/lib/x86_64-linux-gnu/gconv /rootfs/usr/lib/x86_64-linux-gnu/gconv
RUN cd /rootfs/usr/lib/x86_64-linux-gnu/gconv && \
    find . -type f -not -name 'gconv-modules*' -not -name 'ISO8859-1.so' -not -name 'UTF-16.so' -not -name 'UTF-32.so' -not -name 'UNICODE.so' -delete

# 4. COPY CUSTOM LIBRARIES
# Fix: We check where json-c landed. It is likely in x86_64-linux-gnu in the builder.
# We explicitly copy it to the destination x86_64-linux-gnu.
COPY --from=builder /usr/lib/libodbc.so.2 /rootfs/usr/lib/x86_64-linux-gnu/libodbc.so.2
COPY --from=builder /usr/lib/libodbcinst.so.2 /rootfs/usr/lib/x86_64-linux-gnu/libodbcinst.so.2
COPY --from=builder /usr/lib/libtdsodbc.so /rootfs/usr/lib/x86_64-linux-gnu/libtdsodbc.so
COPY --from=builder /usr/lib/libcurl.so.4 /rootfs/usr/lib/x86_64-linux-gnu/libcurl.so.4
# We use a wildcard for json-c source to catch it whether it is in /usr/lib or /usr/lib/x86_64...
COPY --from=builder /usr/lib/*/libjson-c.so.5 /rootfs/usr/lib/x86_64-linux-gnu/libjson-c.so.5

# 5. PRE-COPY SYSTEM LIBRARIES
COPY --from=builder \
    /usr/lib/x86_64-linux-gnu/libltdl.so.7 \
    /usr/lib/x86_64-linux-gnu/liboath.so.0 \
    /usr/lib/x86_64-linux-gnu/

# 6. CONSOLIDATE
RUN cp -v -L \
    /usr/lib/x86_64-linux-gnu/libltdl.so* \
    /usr/lib/x86_64-linux-gnu/liboath.so.0 \
    /usr/lib/x86_64-linux-gnu/libuuid.so.1 \
    /usr/lib/x86_64-linux-gnu/libnss_dns.so* \
    /usr/lib/x86_64-linux-gnu/libnss_files.so* \
    /usr/lib/x86_64-linux-gnu/libresolv.so* \
    /rootfs/usr/lib/x86_64-linux-gnu/

# 7. CONFIGURATION
RUN mkdir -p /rootfs/etc/freetds \
    && echo "hosts: files dns" > /rootfs/etc/nsswitch.conf \
    && printf "[FreeTDS]\nDescription=FreeTDS\nDriver=/usr/lib/x86_64-linux-gnu/libtdsodbc.so\nUsageCount=1\n" > /rootfs/etc/odbcinst.ini \
    && printf "[global]\n\ttext size = 64512\n\tclient charset = UTF-8\n" > /rootfs/etc/freetds/freetds.conf

# 8. PERMISSIONS
RUN echo "cppserver:x:10001:10001::/home/cppserver:/bin/false" >> /rootfs/etc/passwd \
    && echo "cppserver:x:10001:" >> /rootfs/etc/group \
    && mkdir -p /rootfs/app/uploads \
    && mkdir -p /rootfs/tmp && chmod 1777 /rootfs/tmp \
    && chown -R 10001:10001 /rootfs/app

# ==============================================================================
# STAGE 3: FINAL RUNTIME
# ==============================================================================
FROM scratch

COPY --from=chiseler /rootfs /
COPY --from=builder --chown=10001:10001 /src/apiserver /app/apiserver
COPY --from=builder /usr/share/zoneinfo /usr/share/zoneinfo

ENV PORT=8080 \
    LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu \
    TZ=UTC \
    FREETDSCONF=/etc/freetds/freetds.conf \
    TDSVER=7.4

WORKDIR /app
EXPOSE 8080
USER 10001

CMD ["./apiserver"]