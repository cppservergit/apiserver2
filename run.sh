#!/bin/bash

# server configuration
export PORT=8080
export POOL_SIZE=24
export IO_THREADS=4

# database configuration
export DB1="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=demodb;UID=sa;PWD=Basica2024;APP=apiserver;Encryption=off;ClientCharset=UTF-8"
export LOGINDB="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=testdb;UID=sa;PWD=Basica2024;APP=apiserver-login;Encryption=off;ClientCharset=UTF-8"

# cors configuration
export CORS_ORIGINS="null,file://"

# blobs storage configuration
export BLOB_PATH="/home/ubuntu/uploads"

# json web token configuration
export JWT_SECRET="B@asica2024*uuid0998554j93m722pQ"
export JWT_TIMEOUT_SECONDS=300

# remote api configuration
export REMOTE_API_URL="http://localhost:8080"
export REMOTE_API_USER="mcordova"
export REMOTE_API_PASS="basica"

# executable
./apiserver
