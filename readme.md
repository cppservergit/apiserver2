# **APIServer2**

APIServer2 is a high-performance, multi-reactor web server written in modern C++23. It is engineered from the ground up to handle massive concurrent loads with low latency, making it an ideal foundation for scalable and robust backend services. The architecture prioritizes performance and stability through a clean separation of I/O and business logic.

```
                             Client Connections
                                        │
                                        ▼
                       ┌───────────────────────────────────┐
                       │   Linux Kernel (SO_REUSEPORT)     │
                       │ Load balances new connections...  │
                       └───────────────────────────────────┘
                                │               │
              ┌─────────────────┴───────────────┴─────────────────┐
              │                                                   │
              ▼                                                   ▼
┌───────────────────────────┐                       ┌───────────────────────────┐
│   I/O Thread 1 (Reactor)  │                       │   I/O Thread N (Reactor)  │
│ ┌───────────────────────┐ │                       │ ┌───────────────────────┐ │
│ │ Listening Socket (FD) │ │                       │ │ Listening Socket (FD) │ │
│ └───────────────────────┘ │                       │ └───────────────────────┘ │
│ ┌───────────────────────┐ │                       │ ┌───────────────────────┐ │
│ │  epoll_wait() loop    │ │                       │ │  epoll_wait() loop    │ │
│ │ (Accepts & Reads)     │ │                       │ │ (Accepts & Reads)     │ │
│ └───────────────────────┘ │                       │ └───────────────────────┘ │
│             │             │                       │             │             │
│             ▼             │                       │             ▼             │
│ `dispatch_to_worker()`    │                       │ `dispatch_to_worker()`    │
│ (Round-Robin Dispatch)    │                       │ (Round-Robin Dispatch)    │
│   │       │       │       │                       │   │       │       │       │
└─┬─│───────│───────│───────┘                       └─┬─│───────│───────│───────┘
  │ │       │       │                                 │ │       │       │
  ▼ │       ▼       ▼                                 ▼ │       ▼       ▼
┌───┴───┐ ┌───┴───┐ ┌───┴───┐                       ┌───┴───┐ ┌───┴───┐ ┌───┴───┐
│ Task  │ │ Task  │ │ Task  │ ...                   │ Task  │ │ Task  │ │ Task  │
│ Queue │ │ Queue │ │ Queue │                       │ Queue │ │ Queue │ │ Queue │
└───┬───┘ └───┬───┘ └───┬───┘                       └───┬───┘ └───┬───┘ └───┬───┘
    │         │         │                               │         │         │
    ▼         ▼         ▼                               ▼         ▼         ▼
┌─────────┐ ┌─────────┐ ┌─────────┐                 ┌─────────┐ ┌─────────┐ ┌─────────┐
│ Worker  │ │ Worker  │ │ Worker  │ ...             │ Worker  │ │ Worker  │ │ Worker  │
│ Thread 1│ │ Thread 2│ │ Thread M│                 │ Thread 1│ │ Thread 2│ │ Thread M│
└─────────┘ └─────────┘ └─────────┘                 └─────────┘ └─────────┘ └─────────┘
    │         │         │                               │         │         │
    └─────────┼─────────┘                               └─────────┼─────────┘
              │                                                   │
              ▼                                                   ▼
    ┌───────────────────┐                             ┌───────────────────┐
    │  Response Queue   │                             │  Response Queue   │
    │ (MPSC)            │                             │ (MPSC)            │
    └───────────────────┘                             └───────────────────┘
              ▲                                                   ▲
              │                                                   │
┌─────────────┴─────────────┐                       ┌─────────────┴─────────────┐
│ I/O Thread 1 (Reactor)    │                       │ I/O Thread N (Reactor)    │
│ (Processes response queue │                       │ (Processes response queue │
│  and writes to socket)    │                       │  and writes to socket)    │
└───────────────────────────┘                       └───────────────────────────┘
```

## **Core Features**

* **High-Concurrency Multi-Reactor Model:** Utilizes the SO\_REUSEPORT socket option, allowing each I/O thread to have its own listening socket. This enables the kernel to efficiently load-balance incoming connections, eliminating the "thundering herd" problem and maximizing throughput.  
* **Contention-Free Worker Pools:** Each I/O thread has its own private pool of worker threads. Tasks are dispatched via dedicated Single-Producer, Single-Consumer (SPSC) queues, ensuring that a high load on one part of the system does not impact the performance of others.  
* **Modern C++23:** Leverages the latest C++ features for safety, performance, and code clarity, including std::jthread, std::expected, and std::string\_view.  
* **Built-in Observability:** Comes with out-of-the-box monitoring through a set of internal API endpoints, providing real-time insights into the server's health and performance.

## **Quality Control**

APIServer2 has been tested using G++ 14.2 sanitizers and SonarCloud static analysis, specifically:

* `-fsanitize=thread`: Thread-safe, no data races.
* `-fsanitize=address`: Memory safety.
* `-fsanitize=leak`: No memory leaks.
* Top "A" qualification with SonarCloud for the Master branch, no issues at all.

## **Building the Server**

The project uses a Makefile for streamlined compilation.

### **Release Build (Optimized)**

make server

This will create a stripped, optimized executable named server\_app.

### **Debug Build**

make server\_debug

This will create a debug executable named server\_debug\_app with full symbols.

## **Running the Server**

The server is configured via environment variables. Create a run script (e.g., run.sh) to set the required variables before launching the application.

\#\!/bin/bash

\# Server Configuration  
export PORT=8080  
export IO\_THREADS=4  
export POOL\_SIZE=16 \# Total worker threads across all I/O workers  
export CORS\_ORIGINS="http://localhost:3000,https://your-frontend.com"

\# JWT Configuration  
export JWT\_SECRET="your-super-secret-key-that-is-long"  
export JWT\_TIMEOUT\_SECONDS=3600

\# Database Connection Strings (example)  
export DB1="Driver=FreeTDS;SERVER=db.example.com;DATABASE=maindb;..."

\# Run the server  
./server\_app

Make the script executable and run it:

chmod \+x run.sh  
./run.sh

## **Key API Endpoints**

The server provides several internal endpoints for monitoring and health checks:

* GET /metrics: Returns a JSON object with real-time performance metrics, including active connections, pending tasks, and average request latency.  
* GET /ping: A simple health check endpoint that returns a {"status":"OK"} JSON response.  
* GET /version: Returns the server's compiled version number.

### **Ubuntu 24.04 Server Environment Setup**

This document provides the necessary apt commands to configure an Ubuntu 24.04 system for both developing and running your C++ multi-reactor server. The dependencies are derived from the project's Makefile and source code.

### **1\. Development Environment Setup**

This setup installs everything required to compile, debug, and run the server. It includes the compiler, build tools, and the development versions of all required libraries (which include headers and static libraries).

Execute the following commands in your terminal:

\# First, update your package lists to ensure you get the latest versions  
sudo apt-get update

\# Install all required packages in a single command  
sudo apt-get install \-y \\  
    g++-14 \\  
    make \\  
    libssl-dev \\  
    libjson-c-dev \\  
    unixodbc-dev \\  
    tdsodbc \\  
    uuid-dev \\  
    libbacktrace-dev \\  
    libcurl4-openssl-dev \\  
    liboath-dev

#### **Package Breakdown (Development):**

* **g++-14**: The specific version of the GNU C++ compiler needed for C++23 features.  
* **make**: The build automation tool used by your Makefile.  
* **libssl-dev**: Provides the development headers for OpenSSL (-lcrypto), used by your JWT implementation.  
* **libjson-c-dev**: Provides the development headers for the json-c library (-ljson-c), used for JSON parsing.  
* **unixodbc-dev**: Provides the development headers for the unixODBC driver manager (-lodbc), which is the interface used for all database connectivity.  
* **tdsodbc**: Installs the actual FreeTDS ODBC driver (libtdsodbc.so) that unixODBC uses to connect to SQL Server.  
* **uuid-dev**: Provides the development headers for libuuid (-luuid), used for generating UUIDs.  
* **libbacktrace-dev**: Provides the development headers for libbacktrace (-lbacktrace), used for generating stack traces in debug builds.  
* **libcurl4-openssl-dev**: Provides development headers for the cURL library, used for making HTTP/HTTPS client requests from within the server.  
* **liboath-dev**: Provides development headers for the OATH toolkit library, used for generating and validating one-time passwords (e.g., for 2FA).

### **2\. Production Environment Setup**

This setup installs only the runtime shared libraries required to *run* a pre-compiled server executable. It does **not** include compilers or development headers, resulting in a smaller, more secure production environment.

This is ideal for a Docker container or a production virtual machine where you will deploy the server\_app binary.

Execute the following commands in your terminal:

\# First, update your package lists  
sudo apt-get update

\# Install only the runtime libraries  
sudo apt-get install \-y \\  
    libssl3 \\  
    libjson-c5 \\  
    unixodbc \\  
    tdsodbc \\  
    libuuid1 \\  
    libbacktrace0 \\  
    libcurl4 \\  
    liboath0

#### **Package Breakdown (Production):**

* **libssl3**: The runtime shared library for OpenSSL.  
* **libjson-c5**: The runtime shared library for json-c.  
* **unixodbc**: The runtime components for the unixODBC driver manager.  
* **tdsodbc**: The runtime FreeTDS ODBC driver used by unixODBC.  
* **libuuid1**: The runtime shared library for UUID generation.  
* **libbacktrace0**: The runtime shared library for libbacktrace.  
* **libcurl4**: The runtime shared library for cURL.  
* **liboath0**: The runtime shared library for the OATH toolkit.