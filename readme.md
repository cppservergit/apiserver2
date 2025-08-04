# **APIServer2**

APIServer2 is a high-performance, multi-reactor EPOLL based web server written in modern C++23. It is engineered from the ground up to handle massive concurrent loads with low latency, making it an ideal foundation for scalable and robust backend services. The architecture prioritizes performance and stability through a clean separation of I/O and business logic. This is the 2nd generation of APIServer, hence the name. It was written 100% with AI.

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
* **Powerful abstractions:** Very easy to use and efficient abstractions to create Web APIs that execute database stored procedures via ODBC API or invoke REST APIs via HTTPS with libcurl, with integrated JSON Web Token stateless security model.

## **Quality Control**

APIServer2 has been tested using G++ 14.2 sanitizers and SonarCloud static analysis, specifically:

* `-fsanitize=thread`: Thread-safe, no data races.
* `-fsanitize=address`: Memory safety.
* `-fsanitize=leak`: No memory leaks.
* Top "A" [qualification](https://sonarcloud.io/summary/overall?id=cppservergit_apiserver2&branch=main) with SonarCloud for the Main branch, no issues , no code duplication, so security hotspots.
* C++ Core Guidelines compliant, double-checked with SonarCloud and Gemini Pro assessments.

The recommended test and production environment is Ubuntu 24.04 with GCC 14.2.

## **Download Repo and install dependencies**

In your projects or home directory, run:
```
git clone https://github.com/cppservergit/apiserver2.git && \
cd apiserver2 && \
sudo apt install -y g++-14 make libssl-dev libjson-c-dev unixodbc-dev tdsodbc uuid-dev libcurl4-openssl-dev liboath-dev && \
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100
```

This will make the current directory apiserver2, you are ready to compile and run, also G++ 14.2 is the default C++ compiler, test it:
```
pwd && g++ --version
```

You will output like this:
```
/home/ubuntu/apiserver2
g++ (Ubuntu 14.2.0-4ubuntu2~24.04) 14.2.0
Copyright (C) 2024 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

### **Package dependencies breakdown:**

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

## **Building the server**
```
make server
```
This will create a stripped, optimized executable named apiserver.

## **Run the server**

The server is configured via environment variables. Use the provided `run.sh` bash script:

```
chmod +x run.sh
```
```
./run.sh
```
You should see output similar to this:
```
[  INFO  ] [Thread: 128360869079680] [--------] Application starting...
[  INFO  ] [Thread: 128360869079680] [--------] CORS enabled for 2 origin(s).
[  INFO  ] [Thread: 128360869079680] [--------] APIServer2 version 1.0.0 starting on port 8080 with 2 I/O threads and 8 total worker threads.
[  INFO  ] [Thread: 128360869079680] [--------] Assigning 4 worker threads per I/O worker.
```

Use CTRL-C to stop the server
```
INFO  ] [Thread: 126044113238656] [--------] Received signal 2 (Interrupt), shutting down.
[  INFO  ] [Thread: 126044113238656] [--------] Application shutting down gracefully.
```

Upon receiving the usual shutdown signals from the user, operating system or a container manager like Kubernetes or Docker, the server will release all its resources, including memory, threads, sockets, database connections, etc.

## **Test the server**

Run the server with `./run.sh` and then from another terminal session execute this:
```
curl localhost:8080/metrics
```

The output will be something like this:
```
{
  "pod_name": "cpp14",
  "start_time": "2025-08-03T20:03:08",
  "total_requests": 0,
  "average_processing_time_seconds": 0.000000,
  "current_connections": 1,
  "current_active_threads": 0,
  "pending_tasks": 0,
  "thread_pool_size": 8,
  "total_ram_kb": 4007228,
  "memory_usage_kb": 12800,
  "memory_usage_percentage": 0.32
}
```

The `/metrics` endpoint is a built-in observability feature of APIServer2, it will respond immedately even under high load. Other observability endpoints are `/ping` and `/version` if you want to test them with CURL. The `/ping` endpoint is for health-checking by load balancers, also called Ingress services in Cloud containers and Kubernetes.

A bash script using CURL for testing your endpoints is provided in folder `unit-test` this script requires a `/login` and sends the resulting JWT token when invoking the secure endpoints, it is a simple and effective alternative to Postman.

## **Build options**

| make command | Executable program | Result
|-----------------|-----------------|-----------------|
| make server     | apiserver     | Optimized executable for production
| make server_debug     | apiserver_debug     | Debug version, non-optimized, prints debug messages and stack traces in case of errors, may produce lots of logs
| make server_perflog     | apiserver_perflog     | Same as `server_app` but prints performance metrics in logs to identify performance problems
| make server_sanitize_thead     | apiserver_sanitizer_thread     | Detects data races
| make server_sanitize_address     | apiserver_sanitizer_address     | Detects memory problems
| make server_sanitize_leak     | apiserver_sanitizer_leak     | Same as above, plus memory leaks

Whenever you produce a new executable different from `make server` you should edit `run.sh` to invoke the new binary, its name changes depending on the `make` target used to compile. The sanitizer builds are not optimized and include debug symbols (-g). It could be a good idea to deploy in production server_app and server_perflog_app (also optimized for production), and switch between them in `run.sh` if the need arises to obtain performance metrics, a restart will take milliseconds only. Besides server and server_perflog, none of the other `make` targets are intended for production use.

Debug and Performance logs do not have any cost impact, unless you compile with their respective `make` targets, the way the `Makefile` and the C++ programming was done, the code to log these levels `debug` and `perf` won't be compiled if the targets were not used during `make`.

## **Configuring the Server**

The server is configured via environment variables. Create a run script (e.g., run.sh) to set the required variables before launching the application.

```
#!/bin/bash

# server configuration
export PORT=8080
export POOL_SIZE=8
export IO_THREADS=2

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

# executable
./apiserver
```

Use `IO_THREADS` to set the number of threads accepting connections and processing network events, `POOL_SIZE` is the number of worker threads used to run your Web APIs, doing the backend work like database access or invoking remote REST services. This pool is divided between the `IO_THREADS` threads, if you set `8`, then there will be 4 workers for each I/O thread, in a separate pool each group of workers' threads.

Sensitive environment variables, like `JWT_SECRET` or database connection strings like `LOGINDB` can be encrypted using an RSA public key and stored in a .enc file, then provide `private.pem` key by placing it in the same APIServer2 directory, and set the environment variable to the filename ending with `.enc`, then APIServer2 will know how to decrypt this value, something like this:
```
export LOGINDB="logindb.enc"
```

This is a simple and effective method when running OnPrem, when running on Kubernetes or another container service, the orchestation platform will provide means for secure environment variables, which should be transparent to APIServer2.

### **Production Environment Setup**

When installing for production purposes, you only need to install the runtime shared libraries required to run the pre-compiled `apiserver` executable. This instructions are for Ubuntu 24.04. This is for informative purposes only, do not execute these steps for the tutorial.

Always update your package lists before installing:
```
sudo apt-get update
```
Install the required libraries:
```
sudo apt install -y libssl3t64 libjson-c5 unixodbc tdsodbc libuuid1 libcurl4t64 liboath0t64
```

Copy into the same directory the files server_app and run.sh with any .enc files and private.pem if using encrypted environment variables and you are ready to go.

#### **Suggested OnPrem deployment**

Using a single Ubuntu 24.04 VM and LXD native Linux containers you can create a stateless cluster of APIServer2 nodes, each one running as a SystemD service, the native Linux logging service is used by the cluster, you can centralize each node's logs into the host VM and query all the cluster using `journalctl`, this setup is based on built-in Ubuntu's facilities, highly efficient and easy to install and manage.
You can test this whole setup on a single Windows 10 Pro PC using Canonical's Multipass VMs.

```
                  +----------------------------------------+
                  |           External Network             |
                  |              (Internet)                |
                  +----------------------------------------+
                                     | (HTTPS/TLS Traffic)
                                     |
+------------------------------------V------------------------------------------+
|                                                                               |
|  ========================= Host Virtual Machine =======================       |
|                                                                               |
|   +------------------------------------------------------------------+        |
|   |  HAProxy Service                                                 |        |
|   |  - Listens on public IP (e.g., 443)                              |        |
|   |  - Performs TLS Termination (decrypts traffic)                   |        |
|   |  - Load balances requests to internal LXD containers             |        |
|   +------------------------------------------------------------------+        |
|                                     | (Plain HTTP Traffic)                    |
|              +----------------------+----------------------+                  |
|              |                      |                      |                  |
|  +-----------V-----------+  +-------V--------------+  +-----V------------+    |
|  | LXD Container 1       |  | LXD Container 2      |  | LXD Container N  |    |
|  |                       |  |                      |  |                  |    |
|  | +-------------------+ |  | +------------------+ |  | +----------------+    |
|  | | APIServer2        | |  | | APIServer2       | |  | | APIServer2     |    |
|  | | (Instance A)      | |  | | (Instance B)     | |  | | (Instance N)   |    |
|  | +-------------------+ |  | +------------------+ |  | +----------------+    |
|  |                       |  |                      |  |                  |    |
|  +-----------------------+  +----------------------+  +------------------+    |
|                                                                               |
|  ========================================================================     |
|                                                                               |
+-------------------------------------------------------------------------------+
```

## **Hello World**

Make sure the server is not running (CTRL-C).
Edit main.cpp, this is how it looks now:
```
#include "server.hpp"
#include "logger.hpp"
#include "webapi_path.hpp"
#include "sql.hpp"
#include "input_validator.hpp"
#include "util.hpp"
#include "json_parser.hpp"
#include "jwt.hpp"
#include "http_client.hpp"
#include <functional>
#include <algorithm> 
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <string_view> 
#include <ranges>      

using enum http::status;
using enum http::method;


int main() {
    try {
        util::log::info("Application starting...");
        server s;
        s.start();
        util::log::info("Application shutting down gracefully.");

    // FIX (Issue #16): Catch the more specific, dedicated exception.
    } catch (const server_error& e) {
        util::log::critical("A critical server error occurred: {}", e.what());
        return 1;
    } catch (const std::exception& e) {
        util::log::critical("An unexpected error occurred: {}", e.what());
        return 1;
    } catch (...) {
        util::log::critical("An unknown error occurred.");
        return 1;
    }

    return 0;
}

```

Above `main()` you will create a function that implements your Web API:
```
void hello_world([[maybe_unused]] const http::request& req, http::response& res) {
    res.set_body(ok, R"({"message":"Hello, World!"})");
}
```

Right after the declaration of the server variable `server s;` you will register your API:
```
s.register_api(webapi_path{"/hello"}, get, &hello_world, false);
```

We are using some `enums` so we can write abbreviations like `ok` for `http::status::ok` and `get` for `http::method::get`.

Your main.cpp should look like this:
```
#include "server.hpp"
#include "logger.hpp"
#include "webapi_path.hpp"
#include "sql.hpp"
#include "input_validator.hpp"
#include "util.hpp"
#include "json_parser.hpp"
#include "jwt.hpp"
#include "http_client.hpp"
#include <functional>
#include <algorithm> 
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <string_view> 
#include <ranges>      

using enum http::status;
using enum http::method;

//your API implementation
void hello_world([[maybe_unused]] const http::request& req, http::response& res) {
    res.set_body(ok, R"({"message":"Hello, World!"})");
}

int main() {
    try {
        util::log::info("Application starting...");
        
        server s;
        
        //register your API endpoints here
        s.register_api(webapi_path{"/hello"}, get, &hello_world, false);
        
        s.start();
        
        util::log::info("Application shutting down gracefully.");
    } catch (const server_error& e) {
        util::log::critical("A critical server error occurred: {}", e.what());
        return 1;
    } catch (const std::exception& e) {
        util::log::critical("An unexpected error occurred: {}", e.what());
        return 1;
    } catch (...) {
        util::log::critical("An unknown error occurred.");
        return 1;
    }

    return 0;
}
```
Save and execute `make server` to compile and then `./run.sh`.

Now open another terminal window on your VM and run:
```
curl localhost:8080/hello
```

You should see:
```
{"message":"Hello, World!"}
```
