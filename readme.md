# **APIServer2**

APIServer2 is a high-performance, multi-reactor EPOLL based web server written in modern C++23. It is engineered from the ground up to handle massive concurrent loads with low latency, making it an ideal foundation for scalable and robust backend services. The architecture prioritizes performance and stability through a clean separation of I/O and business logic. This is the 2nd generation of APIServer, hence the name. It was written 100% with AI.
```mermaid
graph TD
    Client(Client Connections) --> Kernel

    subgraph OS [Kernel Space]
        style OS fill:#e1edfa,stroke:#333,stroke-dasharray: 5 5
        Kernel[Linux Kernel<br/>SO_REUSEPORT<br/>Load balances new connections]
        style Kernel fill:#333,color:#fff
    end

    %% Connection to Reactor 1 Lane
    Kernel -->|Connection| R1_Top

    %% Connection to Reactor N Lane
    Kernel -->|Connection| RN_Top

    %% ==========================================
    %% PROCESSING LANE 1
    %% ==========================================
    subgraph Lane1 [Processing Lane 1]
        direction TB
        style Lane1 fill:#f9f9f9,stroke:#333

        R1_Top[I/O Thread 1 Reactor<br/>Listening Socket FD<br/>epoll_wait loop]
        style R1_Top fill:#d5e8d4,stroke:#82b366

        R1_Dispatch[dispatch_to_worker<br/>Round-Robin Dispatch]
        
        R1_Top --> R1_Dispatch

        %% Queues and Workers
        subgraph R1_Pool [Worker Pool]
            style R1_Pool fill:#fff,stroke:#ccc
            
            R1_Q1[Task Queue] --> R1_W1[Worker Thread 1]
            R1_Q2[Task Queue] --> R1_W2[Worker Thread 2]
            R1_QM[Task Queue] --> R1_WM[Worker Thread M]
        end

        R1_Dispatch --> R1_Q1
        R1_Dispatch --> R1_Q2
        R1_Dispatch --> R1_QM

        %% Response Queue aggregation
        R1_RespQ[Response Queue<br/>MPSC]
        style R1_RespQ fill:#ffe6cc,stroke:#d79b00

        R1_W1 --> R1_RespQ
        R1_W2 --> R1_RespQ
        R1_WM --> R1_RespQ

        %% Back to IO Thread for writing
        R1_Bot[I/O Thread 1 Reactor<br/>Processes response queue<br/>Writes to socket]
        style R1_Bot fill:#d5e8d4,stroke:#82b366

        R1_RespQ --> R1_Bot
    end

    %% ==========================================
    %% PROCESSING LANE N
    %% ==========================================
    subgraph LaneN [Processing Lane N]
        direction TB
        style LaneN fill:#f9f9f9,stroke:#333

        RN_Top[I/O Thread N Reactor<br/>Listening Socket FD<br/>epoll_wait loop]
        style RN_Top fill:#d5e8d4,stroke:#82b366

        RN_Dispatch[dispatch_to_worker<br/>Round-Robin Dispatch]
        
        RN_Top --> RN_Dispatch

        %% Queues and Workers
        subgraph RN_Pool [Worker Pool]
            style RN_Pool fill:#fff,stroke:#ccc
            
            RN_Q1[Task Queue] --> RN_W1[Worker Thread 1]
            RN_Q2[Task Queue] --> RN_W2[Worker Thread 2]
            RN_QM[Task Queue] --> RN_WM[Worker Thread M]
        end

        RN_Dispatch --> RN_Q1
        RN_Dispatch --> RN_Q2
        RN_Dispatch --> RN_QM

        %% Response Queue aggregation
        RN_RespQ[Response Queue<br/>MPSC]
        style RN_RespQ fill:#ffe6cc,stroke:#d79b00

        RN_W1 --> RN_RespQ
        RN_W2 --> RN_RespQ
        RN_WM --> RN_RespQ

        %% Back to IO Thread for writing
        RN_Bot[I/O Thread N Reactor<br/>Processes response queue<br/>Writes to socket]
        style RN_Bot fill:#d5e8d4,stroke:#82b366

        RN_RespQ --> RN_Bot
    end
```

## **Core Features**

* **High-Concurrency Multi-Reactor Model:** Utilizes the SO\_REUSEPORT socket option, allowing each I/O thread to have its own listening socket. This enables the kernel to efficiently load-balance incoming connections, eliminating the "thundering herd" problem and maximizing throughput.  
* **Contention-Free Worker Pools:** Each I/O thread has its own private pool of worker threads. Tasks are dispatched via dedicated Single-Producer, Single-Consumer (SPSC) queues, ensuring that a high load on one part of the system does not impact the performance of others.  
* **Modern C++23:** Leverages the latest C++ features for safety, performance, and code clarity, including std::jthread, std::expected, and std::string\_view.  
* **Built-in Observability:** Comes with out-of-the-box monitoring through a set of internal API endpoints, providing real-time insights into the server's health and performance.
* **Powerful abstractions:** Very easy to use and efficient abstractions to create Web APIs that execute database stored procedures via ODBC API or invoke REST APIs via HTTPS with libcurl, with integrated JSON Web Token stateless security model.
* **HTTP 1.1 Keep-Alive server:** APIServer2 is a plain HTTP 1.1 server that is designed to run behind a Load Balancer that provides the TLS facade, it provides up to 97% of connection reuse with HAProxy 2.8.x saving lots of resources and CPU time avoiding creation/destruction of sockets between the Load Balancer and the backend servers (APIServer2 instances). This is a common and convenient setup for OnPrem (LXD containers), Kubernetes and Cloud container services. The only HTTP verbs supported are GET, POST and OPTIONS, query parameters are not supported, all data must be sent via POST with JSON or multipart-form-data, nothing else is accepted.

## **Quality Control**

APIServer2 has been tested using G++ 14.2 sanitizers and SonarCloud static analysis, specifically:

* `-fsanitize=thread`: Thread-safe, no data races.
* `-fsanitize=address`: Memory safety.
* `-fsanitize=leak`: No memory leaks.
* Top "A" [qualification](https://sonarcloud.io/summary/overall?id=cppservergit_apiserver2&branch=main) with SonarCloud for the Main branch, no issues , no code duplication, so security hotspots.
* C++ Core Guidelines compliant, double-checked with SonarCloud and Gemini Pro assessments.

The recommended test and production environment is Ubuntu 24.04 with GCC 14.2.

**Note**: To run the thread-sanitizer version you need to run this command, before running APIServer2:
```
sudo sysctl vm.mmap_rnd_bits=30
```

## **Download Repo and install dependencies**

In your projects or home directory, run:
```
git clone https://github.com/cppservergit/apiserver2.git && \
cd apiserver2 && \
sudo apt install -y g++-14 make libssl-dev libjson-c-dev unixodbc-dev tdsodbc uuid-dev libcurl4-openssl-dev liboath-dev && \
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100
chmod +x run.sh
```

This will make the current directory apiserver2, you are ready to compile and run, also G++ 14.2 is the default C++ compiler, test it:
```
pwd && g++ --version
```

You should see output like this:
```
/home/ubuntu/apiserver2
g++ (Ubuntu 14.2.0-4ubuntu2~24.04) 14.2.0
Copyright (C) 2024 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

## **Building the server**
```
make server
```
This will create a stripped, optimized executable named apiserver.

## **Run the server**

The server is configured via environment variables. Use the provided `run.sh` bash script:
```
./run.sh
```
You should see output similar to this:
```
[  INFO  ] [Thread: 127779938138752] [--------] Application starting...
[  INFO  ] [Thread: 127779938138752] [--------] CORS enabled for 2 origin(s).
[  INFO  ] [Thread: 127779938138752] [--------] APIServer2 version 1.1.1 starting on port 8080 with 4 I/O threads and 24 total worker threads.
[  INFO  ] [Thread: 127779938138752] [--------] Assigning 6 worker threads per I/O worker.
```
Use CTRL-C to stop the server
```
INFO  ] [Thread: 126044113238656] [--------] Received signal 2 (Interrupt), shutting down.
[  INFO  ] [Thread: 126044113238656] [--------] Application shutting down gracefully.
```

## **Test the server**

Run the server with `./run.sh` and then from another terminal session on the same host execute this:
```
curl localhost:8080/metrics -H "x-api-key: 6976f434-d9c1-11f0-93b8-5254000f64af" -s | jq
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
To get the version of APIServer2:
```
curl localhost:8080/version -H "x-api-key: 6976f434-d9c1-11f0-93b8-5254000f64af" -s | jq
```
Expected result (version number may vary):
```
{
  "pod_name": "cpp14",
  "version": "1.1.6"
}
```

The `/metrics` endpoint is a built-in observability feature of APIServer2, it will respond immedately even under high load, there is a another version of this API called `/metricsp` to be consumed by Grafana Prometheus. Other observability endpoints are `/ping` for health-checking and `/version`. The `/ping` endpoint is for health-checking by load balancers and Kubernetes.

The diagnostics APIs `/metrics`, `/metricsp` and `/version` are protected by an API-Key which is defined in the `run.sh` script, we suggest using the program `uuid`
to generate your API Key and distribute it to your monitoring agents. Please note that if there is no API-KEY defined in run.sh then the diagnostics APIs will respond without the security check, even if an `x-api-key` header was sent in the request.

Try calling `/metricsp` for Prometheus, the output is plain text, you do not neet `jq` for JSON pretty printing:
```
curl localhost:8080/metricsp -H "x-api-key: 6976f434-d9c1-11f0-93b8-5254000f64af"
```

## **Build options**

| make command | Executable program | Result
|-----------------|-----------------|-----------------|
| make server     | apiserver     | Optimized executable for production
| make server_debug     | apiserver_debug     | Debug version, non-optimized, prints debug messages and stack traces in case of errors, may produce lots of logs
| make server_perflog     | apiserver_perflog     | Same as `make server` but prints performance metrics in logs to identify performance problems
| make server_sanitize_thead     | apiserver_sanitizer_thread     | Detects data races
| make server_sanitize_address     | apiserver_sanitizer_address     | Detects memory problems
| make server_sanitize_leak     | apiserver_sanitizer_leak     | Same as above, plus memory leaks

Whenever you produce a new executable different from `make server` you should edit `run.sh` to invoke the new binary, its name changes depending on the `make` target used to compile. The sanitizer builds are not optimized and include debug symbols (-g). It could be a good idea to deploy in production apiserver and apiserver_perflog (also optimized for production), and switch between them in `run.sh` if the need arises to obtain performance metrics, a restart will take milliseconds only. Besides apiserver and apiserver_perflog, none of the other `make` targets are intended for production use.

## **Configuring the Server**

The server is configured via environment variables. Create a run script (e.g., run.sh) to set the required variables before launching the application.

```
#!/bin/bash

# server configuration
export PORT=8080
export POOL_SIZE=24
export IO_THREADS=4
export QUEUE_CAPACITY=500

# database configuration
export DB1="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=demodb;UID=sa;PWD=Basica2024;APP=apiserver;Encryption=off;ClientCharset=UTF-8"
export LOGINDB="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=testdb;UID=sa;PWD=Basica2024;APP=apiserver-login;Encryption=off;ClientCharset=UTF-8"

# cors configuration
export CORS_ORIGINS="null,file://"

# blobs storage configuration
export BLOB_PATH="/home/ubuntu/uploads"

# json web token configuration
export JWT_SECRET="B@asica2025*uuid0998554j93m722pQ"
export JWT_TIMEOUT_SECONDS=300

# api key for diagnostics
export API_KEY="6976f434-d9c1-11f0-93b8-5254000f64af"

# remote api configuration
export REMOTE_API_URL="https://cppserver.com"
export REMOTE_API_USER="mcordova"
export REMOTE_API_PASS="basica"

# executable
./apiserver
```

Use `IO_THREADS` to set the number of threads accepting connections and processing network events, `POOL_SIZE` is the number of worker threads used to run your Web APIs, doing the backend work like database access or invoking remote REST services. This pool is divided between the `IO_THREADS` threads, if you set `8`, then there will be 4 workers for each I/O thread, in a separate pool each group of workers' threads.

Sensitive environment variables, like `JWT_SECRET` or database connection strings like `LOGINDB` can be encrypted using an RSA public key and stored in a .enc file, then provide `private.pem` key by placing it in the same APIServer2 directory, and set the environment variable to the filename ending with `.enc`, then APIServer2 will know how to decrypt this value, something like this:
```
export LOGINDB="logindb.enc"
```
This is a simple and effective method when running OnPrem, when running on Kubernetes or another container service, the orchestation platform will provide means for secure environment variables, which should be transparent to APIServer2. A [tutorial](https://github.com/cppservergit/apiserver2/blob/main/docs/encrypt.md) is provided for encryption of these environment variables using `openssl`.

## **Hello World**

The file `main.cpp` already contains several Web API examples, in this section you are going to learn how to create different types of APIs, from "Hello World" to database and REST access, with stateless JWT security controls.

### **The basics**
To create a Web API is a 2-step process, first you define a function above `main() {...}` in `main.cpp`, this function implements your Web API logic and must produce a response, which will be JSON most of the time:
```
void hello_world([[maybe_unused]] const http::request& req, http::response& res) {
    res.set_body(ok, R"({"message":"Hello, World!"})");
}
```

Then in main(), you have to register your function in the Web APIs catalog, right after the `server s;` variable:
```
s.register_api(webapi_path{"/hello"}, get, &hello_world, false);
```
**Note**: We are using some `using enum` statements in main.cpp so we can write abbreviations like `ok` for `http::status::ok` and `get` for `http::method::get`.

This is how your `main()` function looks now:
```
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

That's it. Compile with `make server` and your API is ready to be called. The last argument `false` indicates that this API does not require a previous login, otherwise, it will require a JWT token to be sent and it must be valid, this token is returned by the `/login` endpoint, we provide an example of this type of service, but you can implement your own.

Run your server with `./run.sh`, now open another terminal window on your VM and run:
```
curl localhost:8080/hello
```

You should see:
```
{"message":"Hello, World!"}
```

**TIP**: use `curl localhost:8080/hello -s | jq` to format the response.

## **Beyond Hello World, the login API**

**NOTE**: For the rest of the examples you will need to install an SQL Server 2019 on docker and restore a couple of databases, it is a very quick procedure following this [tutorial](https://github.com/cppservergit/apiserver2/blob/main/docs/sqlserver.md), if you are using Canonical's Multipass on Windows 10/11, we suggest using a separate VM to install the demo databases, the run.sh script that configures APIServer2 already contains the ODBC connection strings for these databases (testdb for securiity and demodb for business transactions).

The `login` API requires 1) input validation because it receives the parameters `username` and `password` 2) the function that interacts with some database stored procedure to access a custom security schema and 3) the registration of the API, without security, because this is the API that authenticates and returns the JSON Web Token (JWT).

Above `main() {...}` we will define the validator for login:
```
const validator login_validator{
    rule<std::string>{"username", requirement::required, [](std::string_view s) { return s.length() >= 6 && !s.contains(' '); }, "User must be at least 6 characters long and contain no spaces."},
    rule<std::string>{"password", requirement::required, [](std::string_view s) { return s.length() >= 6 && !s.contains(' '); }, "Password must be at least 6 characters long and contain no spaces."}
};
```
A validator can contain many rules, one for each parameter, a rule specifies the C++ data type, the name, the `required` or `optional` flag, and an optional lambda function that represents a custom validation rule. You API won't be invoked if the validation fails, all rules must pass.

Then the function that implements the API:
```
void login(const http::request& req, http::response& res) {
    const auto user = **req.get_value<std::string>("username");
    const auto password = **req.get_value<std::string>("password");
    const std::string session_id = util::get_uuid();
    const std::string_view remote_ip = req.get_remote_ip();

    sql::resultset rs = sql::query("LOGINDB", "{CALL cpp_dblogin(?,?,?,?)}", user, password, session_id, remote_ip);

    if (rs.empty()) {
        res.set_body(unauthorized, R"({"error":"Invalid credentials"})");
        return;
    }

    const auto& row = rs.at(0);
    if (row.get_value<std::string>("status") == "INVALID") {
        const std::string error_code = row.get_value<std::string>("error_code");
        const std::string error_desc = row.get_value<std::string>("error_description");
        util::log::warn("Login failed for user '{}' from {}: {} - {}", user, remote_ip, error_code, error_desc);
        res.set_body(unauthorized, std::format(R"({{"error":"{}", "description":"{}"}})", error_code, error_desc));
    } else {
        const std::string email = row.get_value<std::string>("email");
        const std::string display_name = row.get_value<std::string>("displayname");
        const std::string role_names = row.get_value<std::string>("rolenames");

        auto token_result = jwt::get_token({
            {"user", user},
            {"email", email},
            {"roles", role_names},
            {"sessionId", session_id}
        });

        if (!token_result) {
            util::log::error("JWT creation failed for user '{}': {}", user, jwt::to_string(token_result.error()));
            res.set_body(internal_server_error, R"({"error":"Could not generate session token."})");
            return;
        }

        const std::map<std::string, std::string, std::less<>> response_data = {
            {"displayname", display_name},
            {"token_type", "bearer"},
            {"id_token", *token_result}
        };
        std::string success_body = json::json_parser::build(response_data);

		util::log::info("Login OK for user '{}': sessionId {} - from {}", user, session_id, remote_ip);

        res.set_body(ok, success_body);
    }
}
```
A very relevant note here, APIServer2 provides an efficient and easy to use ODBC abstraction, in this case we use a cached prepared statement to call a stored procedure with parameters bindings, which is the defacto technique to avoid SQL injection attacks, all APIServer2 SQL facilities use this technique of cached prepared statement, for maximun security and performance:
```
sql::resultset rs = sql::query("LOGINDB", "{CALL cpp_dblogin(?,?,?,?)}", user, password, session_id, remote_ip);
```
Finally the registration in `main()`, notice this time we pass the validator and the function that implements the API, if there is no validator (like with `/hello`) we use a shorter overload of this `register_api(...)` function.
```
s.register_api(webapi_path{"/login"}, post, login_validator, &login, false);
```
Now with the server running on one terminal, go to the second terminal and run this:
```
curl --json '{"username":"mcordova", "password":"basica"}' localhost:8080/login -s | jq
```
You should receive a response like this:
```
{
  "displayname": "Martín Córdova",
  "id_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6Im1hcnRpbi5jb3Jkb3ZhQGdtYWlsLmNvbSIsImV4cCI6IjE3NTQzNjE0NTciLCJpYXQiOiIxNzU0MzYxMTU3Iiwicm9sZXMiOiJzeXNhZG1pbiwgY2FuX2RlbGV0ZSwgY2FuX3VwZGF0ZSIsInNlc3Npb25JZCI6IjY5NTgyZTVlLTE4OTUtNGE0MS1hMGViLTVmN2VjMGI1MjVmNSIsInVzZXIiOiJtY29yZG92YSJ9.wAjwQDfKx1vpZ3JoPyZGotLvDGfEyJeKZ5tTyyd5jB4",
  "token_type": "bearer"
}
```

## **Simple database access**

APIServer2 has a specific SQL API to call a stored procedure that returns JSON, this is very efficient, many databases support returning JSON from a query result, it is convenient to use this method whenever possible.

Here is a simple API implementation that has no validator constraints, just execute the Stored Procedure and retrieve the JSON response, using Modern C++:
```
void get_shippers([[maybe_unused]] const http::request& req, http::response& res) {
    res.set_body(ok, sql::get("DB1", "{CALL sp_shippers_view}").value_or("[]"));
}
```
Then we register `/shippers` in `main(){...}` using the shorter version because we have no validator:
```
s.register_api(webapi_path{"/shippers"}, get, &get_shippers, true);
```
This is a secure method, if you call this API `localhost:8080/shippers` with CURL without passing a header with the JWT security token then the response will be `401 Unauthorized` with a JSON body in the response containing a more specific detail:
```
{"error":"Invalid or missing token"}
```
With a simple and quick 2-liner we can login, capture the JWT token and then invoke the secure API:
```
TOKEN=$(curl --json '{"username":"mcordova", "password":"basica"}' "http://localhost:8080/login" -s | jq -r '.id_token')
curl "http://localhost:8080/shippers" -s -H "Authorization: Bearer $TOKEN" | jq
```
The response will be something like this:
```
[
  {
    "shipperid": 1,
    "companyname": "Speedy Express",
    "phone": "(503) 555-9831"
  },
  {
    "shipperid": 2,
    "companyname": "United Package",
    "phone": "(505) 555-3199"
  },
  {
    "shipperid": 3,
    "companyname": "Federal Shipping",
    "phone": "(503) 555-9931"
  },
  {
    "shipperid": 13,
    "companyname": "Federal Courier Venezuela",
    "phone": "555-6728"
  },
  {
    "shipperid": 501,
    "companyname": "UPS",
    "phone": "500-CALLME"
  },
  {
    "shipperid": 503,
    "companyname": "Century 22 Courier",
    "phone": "800-WE-CHARGE"
  }
]
```
Please note that this JSON is returned straight from the database Stored Procedure, if you are curious about how to write SQL that returns JSON, feel free to examine the provided demo databases, we recommend using DBeaver SQL Client to navigate into the database objects.

Execute the same exercise with `/products`, it is a very similar implementation. Please note that JWT tokens can expire after a few minutes, you may need a fresh token.
```
TOKEN=$(curl --json '{"username":"mcordova", "password":"basica"}' "http://localhost:8080/login" -s | jq -r '.id_token')
curl "http://localhost:8080/products" -s -H "Authorization: Bearer $TOKEN" | jq
```

## **Parameterized SQL queries**

This is very similar to the previous example, but this `/customer` API will return a more complex JSON response from the database and will have its own validator constraints, it will receive a Customer ID parameter and pass it to the stored procedure that executes the query and returns the master/detail JSON output.
The API validator:
```
const validator customer_validator{
    rule<std::string>{"id", requirement::required, 
        [](std::string_view s) {
            return s.length() == 5 && std::ranges::all_of(s, [](unsigned char c){ return std::isalpha(c); });
        }, 
        "Customer ID must be exactly 5 alphabetic characters."
    }
};
```
A validator will contain one rule for each input parameter, regardless if it comes as a GET or a POST HTTP request.

Now the API implementation:
```
void get_customer(const http::request& req, http::response& res) {
    auto id_result = req.get_value<std::string>("id");
    const std::string& customer_id = **id_result;
    const auto json_result = sql::get("DB1", "{CALL sp_customer_get(?)}", customer_id);
    res.set_body(
        json_result ? ok : not_found,
        json_result.value_or(R"({"error":"Customer not found"})")
    );
}
```
The APIServer2 validator contract guarantees that the API function won't be called if the validator does not pass, this way we do not need to check if the `id` field is empty, there is no chance for that. Modern C++ again, simple and elegant logic, it will be `OK (200)` with the resulting JSON response from the database Stored Procedure or `NOT FOUND (404)` with a custom JSON output. In the case of a database error, APIServer2 catches the exception and returns 500 with an error description, leaving a detailed system log record with all the exception details, no application crash.

The API must be registered in `main()`:
```
s.register_api(webapi_path{"/customer"}, post, customer_validator, &get_customer, true);
```
We are using the full overload of the `register_api()` function, we pass the validator and the function address. In this example we accept a parameter, so `post` is only verb we can use for this API, the data must be sent using multipart-form-data or JSON. An API will only accept a request with the HTTP VERB indicated in the `register_api()` call, that is another security clause of the APIServer2 contract, a violation will trigger an exception `400 BAD REQUEST`.

Test it calling `/customer`, as before, we need to login first to capture the JWT token and then pass it to the API:
```
TOKEN=$(curl --json '{"username":"mcordova", "password":"basica"}' "http://localhost:8080/login" -s | jq -r '.id_token')
curl "http://localhost:8080/customer" --json '{"id":"anatr"}' -H "Authorization: Bearer $TOKEN" -s | jq
```
Expected output:
```
{
  "customerid": "ANATR",
  "contactname": "Ana Trujillo",
  "companyname": "Ana Trujillo Emparedados y helados",
  "city": "México D.F.",
  "country": "Mexico",
  "phone": "(5) 555-4729",
  "orders": [
    {
      "customerid": "ANATR",
      "orderid": 10308,
      "orderdate": "1994-10-19",
      "shipcountry": "Mexico",
      "shipper": "Federal Shipping",
      "total": 88.8000
    },
    {
      "customerid": "ANATR",
      "orderid": 10625,
      "orderdate": "1995-09-08",
      "shipcountry": "Mexico",
      "shipper": "Speedy Express",
      "total": 479.7500
    },
    {
      "customerid": "ANATR",
      "orderid": 10759,
      "orderdate": "1995-12-29",
      "shipcountry": "Mexico",
      "shipper": "Federal Shipping",
      "total": 320.0000
    },
    {
      "customerid": "ANATR",
      "orderid": 10926,
      "orderdate": "1996-04-03",
      "shipcountry": "Mexico",
      "shipper": "Federal Shipping",
      "total": 514.4000
    }
  ]
}
```

The stored procedure invoked by this API is an interesting example of using more complex SQL logic to efficiently produce a compact nested JSON response (customer with all its purchase orders).

## **Parameterized SQL queries with dates**

Very similar to the previous example, but in this `/sales` API we accept HTTP POST only and 2 YYYY-MM-DD parameters.
This is the validator:
```
const validator sales_validator{
    rule<std::chrono::year_month_day>{"start_date", requirement::required},
    rule<std::chrono::year_month_day>{"end_date", requirement::required}
};
```

APIServer2 uses C++ data types to indicate the expected value of each parameter, in this case there is an additional rule that cannot be defined with the validator, but it will be enforced inside the API itself, `start_date < end_date`:
```
void get_sales_by_category(const http::request& req, http::response& res) {
    auto start_date = **req.get_value<std::chrono::year_month_day>("start_date");
    auto end_date = **req.get_value<std::chrono::year_month_day>("end_date");

    if (start_date >= end_date) {
        res.set_body(bad_request, R"({"error":"start_date must be before end_date"})");
        return;
    }

    res.set_body(ok, sql::get("DB1", "{CALL sp_sales_by_category(?,?)}", start_date, end_date).value_or("[]"));
}
```
If the inputs do not comply with the custom rule then a `400 BAD REQUEST` will be returned with a JSON body describing the validation error. APIServer2 will also return a `400` if the inputs do not pass the validator rules, that is part of the contract.

To test this `/sales` API you need a valid token, then you can call it like:
```
TOKEN=$(curl --json '{"username":"mcordova", "password":"basica"}' "http://localhost:8080/login" -s | jq -r '.id_token')
curl --json '{"start_date":"1994-01-01","end_date":"1996-12-31"}' "http://localhost:8080/sales" -H "Authorization: Bearer $TOKEN" -s | jq
```
The expected response:
```
[
  {
    "id": 1,
    "item": "Beverages",
    "subtotal": 267868.1800000
  },
  {
    "id": 4,
    "item": "Dairy Products",
    "subtotal": 234507.2850000
  },
  {
    "id": 3,
    "item": "Confections",
    "subtotal": 167357.2250000
  },
  {
    "id": 6,
    "item": "Meat/Poultry",
    "subtotal": 163022.3595000
  },
  {
    "id": 8,
    "item": "Seafood",
    "subtotal": 131261.7375000
  },
  {
    "id": 2,
    "item": "Condiments",
    "subtotal": 106047.0850000
  },
  {
    "id": 7,
    "item": "Produce",
    "subtotal": 99984.5800000
  },
  {
    "id": 5,
    "item": "Grains/Cereals",
    "subtotal": 95744.5875000
  }
]
```
If you try passing an invalid date the server responds with `400 bad request` and a JSON body with the details:
```
{
  "error": "Validation failed for parameter 'start_date': Invalid value: '1994-01-01x'"
}
```
The same for missing required input parameters.

## **Uploads with database access**

This example accepts a `multpart-form-data POST` request with a file named `file1` and a `title` field, uploaded files require special treatment, different from regular request parameters.
For visualization purposes only, this would be an HTML representation of a frontend for this API, :
```
<form id="form1">
    <input type="text" name="title" size="30" required/>
    <input type="file" name="file1" required>
    <input type="submit">
</form>
```

The validator:
```
const validator upload_validator{
    rule<std::string>{"title", requirement::required}
};
```

The uploaded field will be processed and validated inside the API implementation. This function checks if the path to store blobs had been configured, then proceeds to check if the upload `file1` is present, if not a `400 bad request` will be returned. If all the prerequisites are in order, the uploaded file will be stored with a unique name (UUID based) in the configured location and a stored procedure will be invoked to store the blob's metadata. File I/O errors will be detected by this function and properly logged, and a `500 server error` will be returned, this is different from the rest of the example APIs, we choose to handle this exception inside this API, it won't escalate to APIServer2 handlers. 
```
void upload_file(const http::request& req, http::response& res) {
    using enum http::status;
    const auto blob_path_str = env::get<std::string>("BLOB_PATH", "");
    if (blob_path_str.empty()) {
        util::log::error("BLOB_PATH environment variable is not set.");
        res.set_body(internal_server_error, R"({"error":"File upload is not configured on the server."})");
        return;
    }
    const std::filesystem::path blob_path(blob_path_str);

    const http::multipart_item* file_part = req.get_file_upload("file1");
    if (!file_part) {
        res.set_body(bad_request, R"({"error":"Missing 'file1' part in multipart form data."})");
        return;
    }
    
    const auto title = **req.get_value<std::string>("title");

    try {
        std::filesystem::create_directories(blob_path);
        const std::filesystem::path original_filename(file_part->filename);
        const std::string new_filename = util::get_uuid() + original_filename.extension().string();
        const std::filesystem::path dest_path = blob_path / new_filename;

        util::log::info("Saving uploaded file '{}' as '{}' with title '{}'", file_part->filename, dest_path.string(), title);

        std::ofstream out_file(dest_path, std::ios::binary);
        if (!out_file) {
            throw file_system_error(std::format("Could not open destination file for writing: {}", util::str_error_cpp(errno)));
        }
        
        out_file.write(file_part->content.data(), file_part->content.size());
        if (!out_file) {
            throw file_system_error(std::format("An error occurred while writing to the destination file: {}", util::str_error_cpp(errno)));
        }
        
        out_file.close();

        sql::exec(
            "DB1",
            "{call sp_blob_add(?, ?, ?, ?, ?)}",
            title,
            new_filename,
            file_part->filename,
            file_part->content_type,
            file_part->content.size()
        );

        const std::map<std::string, std::string, std::less<>> response_data = {
            {"title", title},
            {"originalFilename", std::string(file_part->filename)},
            {"savedFilename", new_filename},
            {"size", std::to_string(file_part->content.size())}
        };
        std::string success_body = json::json_parser::build(response_data);
        res.set_body(ok, success_body);

    } catch (const file_system_error& e) {
        util::log::error("File upload failed: {}", e.what());
        res.set_body(internal_server_error, R"({"error":"Failed to save uploaded file."})");
    }
}
```
A quick test with your 2-liner, you need a file test.txt, something like `echo "Hello World APIServer2" > test.txt` in the current directory:
```
TOKEN=$(curl --json '{"username":"mcordova", "password":"basica"}' "http://localhost:8080/login" -s | jq -r '.id_token')
curl "http://localhost:8080/upload" -F "file1=@test.txt" -F "title=My little text file" -s -H "Authorization: Bearer $TOKEN" | jq
```
You should see a response similar to this:
```
{
  "originalFilename": "test.txt",
  "savedFilename": "cc4712f7-788c-4b47-8782-aaa02011f02b.txt",
  "size": "24",
  "title": "My little text file"
}
```
In your `run.sh` script you configure the location to store the blobs, if you run several times the test you will see several files like these:
```
uploads
├── 8e603bf2-384c-4bc4-971e-26b94ad6d8a1.sh
├── e305a715-297f-419e-913c-3edf5a468724.sh
├── e6a25496-664b-4b59-9e1a-769b4d46ce18.sh
└── f70868e0-8df1-46be-a066-90569af50751.sh
```
On your server's terminal you will see a log entry for the upload activity:
```
[  INFO  ] [Thread: 124822654793408] [--------] Saving uploaded file 'test.txt' as '/home/ubuntu/uploads/cc4712f7-788c-4b47-8782-aaa02011f02b.txt' with title 'My little text file'
```
The storage location defined in run.sh is just a path, in our case it is a local storage, but it could be mapped to a centralized storage, like NFS or MinIO (S3), in these cases additional configuration is required to map the path to the storage service, that mapping it transparent to APIServer2, it only sees a local path just like when using local storage. Kubernetes, Docker and Cloud services provide the facilities to define paths that look like local storage to the containers.

## **Calling a remote REST API**
In this section we study the code required to create an API that instead of calling a database stored procedure, it will call a remote API via HTTP/HTTPS, for simplicity's sake we will invoke our own local APIServer2 `/customer` API, this is a secure API, so we have to login, extract the token and then call the API, we will use a helper class `RemoteCustomerService` defined at the top of `main.cpp`. This class uses the module `http_client` which is a convenient wrapper of the native libcurl library. We also provide a custom exception for all errors thrown from this class code.
```
/**
 * @brief Custom exception for errors originating from the RemoteCustomerService.
 */
class RemoteServiceError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * @class RemoteCustomerService
 * @brief A stateless service class to interact with a remote customer API.
 */
class RemoteCustomerService {
public:
    /**
     * @brief Fetches customer information from the remote API.
     */
    static http_response get_customer_info(std::string_view customer_id) {
        const std::string uri = "/customer";
        const std::string token = login_and_get_token();
        const std::map<std::string, std::string, std::less<>> payload = {
            {"id", std::string(customer_id)}
        };
        const std::string body = json::json_parser::build(payload);
        const std::map<std::string, std::string, std::less<>> headers = {
            {"Authorization", std::format("Bearer {}", token)},
            {"Content-Type", "application/json"}
        };

        util::log::debug("Fetching remote customer info from {} with payload {}", uri, body);
        
        // Create a transient http_client for this specific request.
        http_client client;
        const auto response = client.post(get_url() + uri, body, headers);
        if (response.status_code != 200) {
            util::log::error("Remote API {} failed with status {}: {}", uri, response.status_code, response.body);
            throw RemoteServiceError("Remote service invocation failed.");
        }
        return response;
    }

private:
    /**
     * @brief Authenticates with the remote API and returns a JWT.
     */
    static std::string login_and_get_token() {
        const std::map<std::string, std::string, std::less<>> login_payload = {
            {"username", get_user()},
            {"password", get_pass()}
        };
        const std::string login_body = json::json_parser::build(login_payload);

        util::log::debug("Logging into remote API at {}", get_url());

        // Create a transient http_client for the login request.
        http_client client;
        const http_response login_response = client.post(get_url() + "/login", login_body, {{"Content-Type", "application/json"}});

        if (login_response.status_code != 200) {
            util::log::error("Remote API login failed with status {}: {}", login_response.status_code, login_response.body);
            throw RemoteServiceError("Failed to authenticate with remote service.");
        }

        json::json_parser token_parser(login_response.body);
        const std::string id_token(token_parser.get_string("id_token"));

        if (id_token.empty()) {
            util::log::error("Remote API login response did not contain an id_token.");
            throw RemoteServiceError("Invalid response from remote authentication service.");
        }
        return id_token;
    }

    // --- Configuration Getters using thread-safe function-local statics ---
    static const std::string& get_url()  { static const std::string url = env::get<std::string>("REMOTE_API_URL"); return url; }
    static const std::string& get_user() { static const std::string user = env::get<std::string>("REMOTE_API_USER"); return user; }
    static const std::string& get_pass() { static const std::string pass = env::get<std::string>("REMOTE_API_PASS"); return pass; }
};
```

Now we need to define the API function implementation:
```
//invokes remote REST API to get customer info
void get_remote_customer(const http::request& req, http::response& res) {
    const auto customer_id = **req.get_value<std::string>("id");

    try {
        // No instance is needed; call the static method directly on the class.
        const http_response customer_response = RemoteCustomerService::get_customer_info(customer_id);
        res.set_body(ok, customer_response.body);

    } catch (const env::error& e) {
        util::log::critical("Missing environment variables for remote API: {}", e.what());
        res.set_body(internal_server_error, R"({"error":"Remote API is not configured on the server."})");
    } catch (const curl_exception& e) {
        util::log::error("HTTP client error while calling remote API: {}", e.what());
        res.set_body(internal_server_error, R"({"error":"A communication error occurred with a remote service."})");
    } catch (const json::parsing_error& e) {
        util::log::error("Failed to parse JSON response from remote API: {}", e.what());
        res.set_body(internal_server_error, R"({"error":"Received an invalid response from a remote service."})");
    } catch (const RemoteServiceError& e) {
        util::log::error("A remote service logic error occurred: {}", e.what());
        res.set_body(internal_server_error, R"({"error":"A logic error occurred while communicating with a remote service."})");
    }
}
```
As you can see, most of the code is dedicated to catching potential errors, leaving detailed log traces and sending an appropiate response to the client. The intent of this code is to serve as a good-enough template to call remote APIs via HTTP(S), providing the basic required building blocks, like login and JWT token extraction, and the actual call to the API with error checking.

For certain exceptions, like `env::error` you may want to log it as `critical` as in the code above, because this signals a server configuration error that should not happen.

Now we have to register this API in `main()`:
```
s.register_api(webapi_path{"/rcustomer"}, post, customer_validator, &get_remote_customer, true);
```
We are reusing the same `customer_validator` from the previous example of the `/customer` API because this remote caller is merely a wrapper of that service, it has the same input definitions.

We need to configure `run.sh` to add server location and login credentials:
```
# remote api configuration
export REMOTE_API_URL="http://localhost:8080"
export REMOTE_API_USER="mcordova"
export REMOTE_API_PASS="basica"
```
Remember that these security-sensitive environment variables can be managed with public/private key pair encryption.

For testing the API use your qtest.sh script to make it easier, just change the last line that invokes the API with curl:
```
TOKEN=$(curl --json '{"username":"mcordova", "password":"basica"}' "http://localhost:8080/login" -s | jq -r '.id_token')
curl "http://localhost:8080/rcustomer" --json '{"id":"anatr"}' -H "Authorization: Bearer $TOKEN" -s | jq
```
It may take a second or two, because the remote API is located in New York.

### **About using self-signed certificates**
The `http_client` wrapper module makes safe use if libcurl, it means it among other things that it won't accept invalid certificates, otherwise APIServer2 would not pass the strict rules of SonarCloud static analysis. If you want to use a self-signed certificate the you must change `http_client.cpp` the function `http_client::impl::configure_common_options` and add these lines:
```
curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
```

**NOTE**: APIServer2 uses a `libcurl` wrapper and makes proper use of keep-alive to save client connections, if you are using a lot of requests to REST APIs you may need to tune your OS for reusing sockets quickly and expanding the port range available to APIServer2 when acting as an HTTP client. When deploying APIServer2 on Kubernetes the provided YAML file already tunes these parameters for you.

## **Docker**

Install docker on your development computer:
```
curl -fsSL https://get.docker.com -o get-docker.sh && sh get-docker.sh && sudo sh get-docker.sh
```

It is recommended to add you user to the `docker` group so you don't have to type `sudo` every time you run a docker command, also the docker targets in Makefile assume you don't need sudo.
```
sudo usermod -aG docker $USER
```
You have to exit the Linux shell and login again to apply the changes, if you are doing this in VSCode terminal, you must reboot the Linux VM and restart VSCode.

Test it:
```
docker images
```

### **Running APIServer2 with Docker**


If you want to run APIServer2 with the default API samples with docker, there is an image in dockerhub with the latest version, for a quick test:
```
docker run -d \
  --name apiserver2 \
  --restart unless-stopped \
  --network host \
  -v /home/ubuntu/uploads:/app/uploads \
  -e POOL_SIZE=24 \
  -e IO_THREADS=4 \
  -e QUEUE_CAPACITY=500 \
  -e DB1="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=demodb;UID=sa;PWD=Basica2024;APP=apiserver;Encryption=off;ClientCharset=UTF-8" \
  -e LOGINDB="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=testdb;UID=sa;PWD=Basica2024;APP=apiserver-login;Encryption=off;ClientCharset=UTF-8" \
  -e CORS_ORIGINS="null,file://" \
  -e JWT_SECRET="B@asica2025*uuid0998554j93m722pQ" \
  -e JWT_TIMEOUT_SECONDS=300 \
  -e API_KEY="6976f434-d9c1-11f0-93b8-5254000f64af" \
  -e REMOTE_API_URL="http://localhost:8080" \
  -e REMOTE_API_USER="mcordova" \
  -e REMOTE_API_PASS="basica" \
  \
  cppserver/apiserver2:latest
```
This image is intended to be used with Kubernetes and Cloud container services.
When running on Kubernetes, all those security-sensitive environment variables will be stored as Kubernet secrets, this is transparent to APIServer2, it will always look for the value of the environment variable, if running the container on the Cloud using a serverless container service, you will probably use some kind of encrypted environment variables, again transparent to APIServer2.

#### **Testing your cointainer**
If you are running on your development machine and your current working directory is /apiserver2, use this command to run the unit tests using the CURL script:
```
unit-test/test.sh
```
Expected output:
```
GET /shippers                       200    true
GET /products                       200    true
GET /metrics                        200    true
GET /version                        200    true
GET /ping                           200    true
POST /customer                      200    true
POST /customer                      200    true
POST /customer                      200    true
POST /customer                      200    true
POST /customer                      200    true
POST /customer                      200    true
POST /customer                      200    true
POST /sales                         200    true
POST /sales                         200    true
POST /sales                         200    true
POST /rcustomer                     200    true
```
If your server is on another port or machine pass the base URL to the `test.sh` script, for example:
```
unit-test/test.sh http://yourVM:8080
```

### **Building your own Docker image**

First we create an optimized but also more neutral binary for the docker image:
```
make clean && make release CXXFLAGS_RELEASE="-O2 -march=x86-64-v3 -DNDEBUG -flto=4"
```

Now we create the image, The APIServer2 repo already includes a `Dockerfile` in the root folder of this project:
```
docker build -t cppserver/apiserver2:latest .
```

To export your Docker image to a compressed .tgz file that can be transferred to MicroK8s, run this command in your terminal:
```
docker save cppserver/apiserver2:latest | gzip > apiserver2.tar.gz
```
Once you have the file, you can import it directly into the MicroK8s registry:
```
microk8s images import apiserver2.tar.gz
```
This avoids the need to push to Docker Hub if your MicroK8s cluster is local or has access to the file.

### **Dockerfile**
```
# ==============================================================================
# Runtime Stage Only
# ==============================================================================
FROM ubuntu:24.04

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install Runtime Dependencies
# These must match the libraries your binary was linked against.
# Note: We REMOVED the user creation steps because 'ubuntu' (UID 1000) exists by default.
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libcurl4 \
    libssl3 \
    libuuid1 \
    libjson-c5 \
    liboath0t64 \
    unixodbc \
    tdsodbc \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Create the uploads directory for BLOB_PATH
# We use the existing 'ubuntu' user (UID 1000)
RUN mkdir -p /app/uploads && chown ubuntu:ubuntu /app/uploads

# --- COPY THE BINARY ---
COPY --chown=ubuntu:ubuntu apiserver /app/apiserver

# Ensure it is executable
RUN chmod +x /app/apiserver

# Define the mount point for Kubernetes PVCs
VOLUME ["/app/uploads"]

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------

# 1. Non-Sensitive Defaults
ENV PORT=8080
ENV POOL_SIZE=24
ENV IO_THREADS=4
ENV QUEUE_CAPACITY=500
ENV CORS_ORIGINS="null,file://"
ENV BLOB_PATH="/app/uploads"
ENV JWT_TIMEOUT_SECONDS=300
ENV REMOTE_API_URL="http://localhost:8080"

# 2. Security Sensitive Variables (Empty for injection via K8s Secrets)
ENV DB1=""
ENV LOGINDB=""
ENV JWT_SECRET=""
ENV API_KEY=""
ENV REMOTE_API_USER=""
ENV REMOTE_API_PASS=""

# Expose port
EXPOSE 8080

# Switch to the existing non-root user 'ubuntu' (UID 1000)
USER ubuntu

# Start the server
CMD ["./apiserver"]
```

## Kubernetes

MicroK8s is the recommended way to deploy APIServer2 for production OnPrem, it is very flexible and will adapt nicely to your resources availability (VMs, cores, RAM, bare-metal, etc).

Now that you have a docker image you can deploy the container to any Kubernetes service or Container service in the Cloud, like AWS ECS or Azure Container Apps, but for local development and testing purposes and also for inexpensive OnPrem kubernetes you can use MicroK8s from Canonical, the best fit for Ubuntu VMs.

```mermaid
flowchart LR
    %% Define styles for better visualization
    classDef ubuntu fill:#E95420,stroke:#333,stroke-width:2px,color:white,font-weight:bold;
    classDef mk8s fill:#326ce5,stroke:#333,stroke-width:2px,color:white;
    classDef pod fill:#d2e3fc,stroke:#1565c0,color:black,font-weight:bold;
    classDef external fill:#f9f9f9,stroke:#666,stroke-width:2px,stroke-dasharray: 5 5,color:black;

    subgraph Host ["Host Infrastructure"]
        style Host fill:#fff,stroke:#ccc
        
        subgraph UbuntuVM ["Ubuntu 24.04 LTS VM"]
            style UbuntuVM fill:#fff5f0,stroke:#E95420,stroke-width:3px

            subgraph MicroK8s ["MicroK8s Single-Node Cluster"]
                style MicroK8s fill:#e8f0fe,stroke:#326ce5,stroke-width:3px
                
                AppPod("Your Application Pod<br/>(Client)"):::pod
            end
        end
    end

    subgraph Backends ["External Backends"]
        style Backends fill:#fff,stroke:#ccc
        
        RestServer["REST API Server"]:::external
        DBServer["Database Server<br/>(e.g., PostgreSQL/MSSQL)"]:::external
    end

    %% Connections
    AppPod -->|"HTTPS Connection<br/>(Port 443)"| RestServer
    AppPod -->|"ODBC Traffic<br/>(TCP/IP)"| DBServer
```

You are invited to follow our step-by-step tutorial on deploying APIServer2 on MicroK8s on a single VM, very easy and quick, in 20 minutes or less you are ready to go:

[Running APIServer2 on MicroK8s](https://github.com/cppservergit/apiserver2/blob/main/docs/microk8s.md)

## Linux native deployment

If you prefer a native Linux solution that fits a single VM with low resources, consider LXD containers, we provide a complete tutorial to setup in minutes a single VM with HAProxy as the Load Balancer and 2 LXD containers running APIServer2 as a Linux SystemD service.

[APiServer2 on LXD with HAProxy](https://github.com/cppservergit/apiserver2/blob/main/docs/lxd.md)

