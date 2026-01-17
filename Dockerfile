FROM ubuntu:24.04

# Prevent interactive prompts during build
ENV DEBIAN_FRONTEND=noninteractive

# 1. Install Runtime Dependencies & Harden Image
# CRITICAL FIX: We must run 'autoremove' and 'clean' BEFORE we purge 'tar' and 'gpgv'.
# Once 'tar' is removed, apt/dpkg become unstable, so that must be the final step.
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libcurl4 \
    libssl3 \
    libuuid1 \
    libjson-c5 \
    liboath0t64 \
    unixodbc \
    tdsodbc \
    tzdata \
    tzdata-legacy \
    # 1. Clean up apt caches while the package manager still works
    && apt-get autoremove -y \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* \
    # 2. --- HARDENING START ---
    # Now that apt is done, we forcibly remove the dangerous tools.
    # We use '|| true' to ensure the build succeeds even if 'git' or 'wget' 
    # were never installed in the first place.
    && (dpkg --purge --force-all gnupg gpgv tar wget git mount || true)
    # --- HARDENING END ---

# 2. Create the Non-Root User (UID 10001)
RUN groupadd -g 10001 cppserver && \
    useradd -u 10001 -g cppserver -s /bin/false -m cppserver

# Set working directory
WORKDIR /app

# 3. Setup Permissions
RUN mkdir -p /app/uploads && chown 10001:10001 /app/uploads

# 4. Copy the Binary
COPY --chown=10001:10001 apiserver /app/apiserver

# Ensure it is executable
RUN chmod +x /app/apiserver

# Define the mount point for Kubernetes PVCs
VOLUME ["/app/uploads"]

# ------------------------------------------------------------------------------
# Configuration (Environment Defaults)
# ------------------------------------------------------------------------------
ENV PORT=8080
ENV POOL_SIZE=24
ENV IO_THREADS=4
ENV QUEUE_CAPACITY=500
ENV CORS_ORIGINS="null,file://"
ENV BLOB_PATH="/app/uploads"
ENV JWT_TIMEOUT_SECONDS=300
ENV REMOTE_API_URL="https://cppserver.com"

# Expose port
EXPOSE 8080

# 5. Switch to Non-Root User
USER 10001

# Start the server
CMD ["./apiserver"]
