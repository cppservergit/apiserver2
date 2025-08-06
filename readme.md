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
chmod +x run.sh
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

Whenever you produce a new executable different from `make server` you should edit `run.sh` to invoke the new binary, its name changes depending on the `make` target used to compile. The sanitizer builds are not optimized and include debug symbols (-g). It could be a good idea to deploy in production apiserver and apiserver_perflog (also optimized for production), and switch between them in `run.sh` if the need arises to obtain performance metrics, a restart will take milliseconds only. Besides apiserver and apiserver_perflog, none of the other `make` targets are intended for production use.

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

**NOTE**: For the rest of the examples you will need to install an SQL Server 2019 on docker and restore a couple of databases, it is a very quick procedure following this [tutorial](https://github.com/cppservergit/apiserver-odbc/blob/main/sqlserver.md), if you are using Canonical's Multipass on Windows 10/11, we suggest using a separate VM to install the demo databases, the run.sh script that configures APIServer2 already contains the ODBC connection strings for these databases (testdb for securiity and demodb for business transactions).

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
The we register `/shippers` in `main(){...}` using the shorter version because we have no validator:
```
s.register_api(webapi_path{"/shippers"}, get, &get_shippers, true);
```
This is a secure method, if you call this API `localhost:8080/shippers` with CURL without passing a header with the JWT security token then the response will be `401 Unauthorized` with a JSON body in the response containing a more specific detail:
```
{"error":"Invalid or missing token"}
```
You need to `/login`, then copy the token from the response and execute CURL like this (use your current token):
```
curl localhost:8080/shippers -H "Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6Im1hcnRpbi5jb3Jkb3ZhQGdtYWlsLmNvbSIsImV4cCI6IjE3NTQ0MDg2MTkiLCJpYXQiOiIxNzU0NDA4MzE5Iiwicm9sZXMiOiJzeXNhZG1pbiwgY2FuX2RlbGV0ZSwgY2FuX3VwZGF0ZSIsInNlc3Npb25JZCI6IjNjMjlkYTc0LWQwOGItNGU3NS1iYjllLTQ2Zjg5YTkyY2M0MCIsInVzZXIiOiJtY29yZG92YSJ9.W9clBi-Ndyx8SmcPGCLKYCYaJlLv-N4D7Pj51ticuXQ"
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
s.register_api(webapi_path{"/customer"}, get, customer_validator, &get_customer, true);
```
We are using the full overload of the `register_api()` function, we pass the validator and the function address. In this example we accept an HTTP GET, we will receive the inputs via URI request parameters, if we pass `post` the data must be sent via multipart-form-data or JSON only. An API will only accept a request with the HTTP VERB indicated in the `register_api()` call, another clause of the APIServer2 contract.

Test it calling `/customer?id=ANATR` with CURL, it is a secure API, you need to `/login`, then copy the token from the response and execute CURL like this (use your current token):
```
curl localhost:8080/customer?id=ANATR -H "Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6Im1hcnRpbi5jb3Jkb3ZhQGdtYWlsLmNvbSIsImV4cCI6IjE3NTQ0MDg2MTkiLCJpYXQiOiIxNzU0NDA4MzE5Iiwicm9sZXMiOiJzeXNhZG1pbiwgY2FuX2RlbGV0ZSwgY2FuX3VwZGF0ZSIsInNlc3Npb25JZCI6IjNjMjlkYTc0LWQwOGItNGU3NS1iYjllLTQ2Zjg5YTkyY2M0MCIsInVzZXIiOiJtY29yZG92YSJ9.W9clBi-Ndyx8SmcPGCLKYCYaJlLv-N4D7Pj51ticuXQ" -s | jq
```
The stored procedure invoked by this API is an interesting example of using more complex SQL logic to efficiently produce a compact JSON response.
