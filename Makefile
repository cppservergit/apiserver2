# Compiler
CXX = g++

# --- Build Configurations ---
.DEFAULT_GOAL := help
CXXFLAGS_BASE = -std=c++23 -Wall -Wextra

CXXFLAGS_DEBUG = -g -DENABLE_DEBUG_LOGS -DUSE_STACKTRACE
CXXFLAGS_RELEASE = -O2 -march=native -DNDEBUG -flto=4
# FIX: Add new perflog build flags
CXXFLAGS_PERFLOG = -O2 -march=native -DNDEBUG -flto=4 -DENABLE_PERF_LOGS
CXXFLAGS_SANITIZER_ADDRESS = -g -fsanitize=address -fno-omit-frame-pointer -O1
CXXFLAGS_SANITIZER_THREAD = -g -fsanitize=thread
CXXFLAGS_SANITIZER_LEAK = -g -fsanitize=address -fsanitize=leak -fno-omit-frame-pointer -O0

LDFLAGS_SANITIZER_ADDRESS = -fsanitize=address
LDFLAGS_SANITIZER_THREAD = -fsanitize=thread
LDFLAGS_SANITIZER_LEAK = -fsanitize=leak

# --- Project Structure ---
SRC_DIR = src
OBJ_DIR = obj

# --- Target Executable Names (Artifacts) ---
TARGET_SERVER_RELEASE = apiserver
TARGET_SERVER_DEBUG = apiserver_debug
# FIX: Add new perflog target executable
TARGET_SERVER_PERFLOG = apiserver_perflog
TARGET_SERVER_SANITIZER_ADDRESS = apiserver_sanitizer_address
TARGET_SERVER_SANITIZER_THREAD = apiserver_sanitizer_thread
TARGET_SERVER_SANITIZER_LEAK = apiserver_sanitizer_leak

# --- Source File Lists ---
SERVER_SRCS = main.cpp
COMMON_LIB_SRCS = http_client.cpp http_request.cpp json_parser.cpp pkeyutil.cpp sql.cpp jwt.cpp
SERVER_LIB_SRCS = server.cpp

# --- Object File Definitions ---
define GET_OBJS
$(patsubst %.cpp,$(OBJ_DIR)/$(1)/%.o,$(2))
endef

# --- Libraries to Link ---
LIBS = -lcurl -ljson-c -lcrypto -lstdc++exp -lbacktrace -lodbc -luuid

# --- User-Facing Commands ---
.PHONY: all release debug server run run_server clean help \
		apiserver apiserver_debug apiserver_perflog apiserver_sanitize_address apiserver_sanitize_thread apiserver_sanitize_leak \
		run_apiserver_debug run_apiserver_perflog run_apiserver_sanitizer_address run_apiserver_sanitizer_thread run_apiserver_sanitizer_leak

all: release

release:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_SERVER_RELEASE)

debug:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_SERVER_DEBUG)

server:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_SERVER_RELEASE)

server_debug:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_SERVER_DEBUG)

# FIX: Add new user-facing target
server_perflog:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_SERVER_PERFLOG)

server_sanitize_address:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_SERVER_SANITIZER_ADDRESS)

server_sanitize_thread:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_SERVER_SANITIZER_THREAD)

server_sanitize_leak:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_SERVER_SANITIZER_LEAK)

run: release
	./$(TARGET_SERVER_RELEASE)

run_server: server
	./$(TARGET_SERVER_RELEASE)

run_server_debug: server_debug
	./$(TARGET_SERVER_DEBUG)

# FIX: Add new run target
run_server_perflog: server_perflog
	./$(TARGET_SERVER_PERFLOG)

run_server_sanitizer_address: server_sanitize_address
	./$(TARGET_SERVER_SANITIZER_ADDRESS)

run_server_sanitizer_thread: server_sanitize_thread
	./$(TARGET_SERVER_SANITIZER_THREAD)

run_server_sanitizer_leak: server_sanitize_leak
	./$(TARGET_SERVER_SANITIZER_LEAK)

clean:
	@echo "==> Cleaning project..."
	rm -rf $(OBJ_DIR) $(wildcard apiserver*)

help:
	@clear
	@echo "Usage: make [target]"
	@echo ""
	@echo "Test Runner Targets:"
	@echo "  server              Build the release server."
	@echo "  server_debug        Build the debug server."
	@echo "  server_perflog      Build the release server with performance logging."
	@echo "  server_sanitize_address Build server with AddressSanitizer."
	@echo "  server_sanitize_thread Build server with ThreadSanitizer."
	@echo "  server_sanitize_leak Build server with LeakSanitizer."
	@echo ""
	@echo "Other Targets:"
	@echo "  clean               Remove all build artifacts."


# --- Linking Rules (for build artifacts) ---
$(TARGET_SERVER_RELEASE): $(call GET_OBJS,release,$(SERVER_SRCS)) $(call GET_OBJS,release,$(COMMON_LIB_SRCS)) $(call GET_OBJS,release,$(SERVER_LIB_SRCS))
	@echo "==> Linking release server: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_RELEASE) -o $@ $^ $(LIBS)
	@echo "==> Stripping symbols..."
	strip $@

$(TARGET_SERVER_DEBUG): $(call GET_OBJS,debug,$(SERVER_SRCS)) $(call GET_OBJS,debug,$(COMMON_LIB_SRCS)) $(call GET_OBJS,debug,$(SERVER_LIB_SRCS))
	@echo "==> Linking debug server: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_DEBUG) -o $@ $^ $(LIBS)

# FIX: Add new linking rule for the perflog server
$(TARGET_SERVER_PERFLOG): $(call GET_OBJS,perflog,$(SERVER_SRCS)) $(call GET_OBJS,perflog,$(COMMON_LIB_SRCS)) $(call GET_OBJS,perflog,$(SERVER_LIB_SRCS))
	@echo "==> Linking performance log server: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_PERFLOG) -o $@ $^ $(LIBS)
	@echo "==> Stripping symbols..."
	strip $@

$(TARGET_SERVER_SANITIZER_THREAD): $(call GET_OBJS,sanitize_thread,$(SERVER_SRCS)) $(call GET_OBJS,sanitize_thread,$(COMMON_LIB_SRCS)) $(call GET_OBJS,sanitize_thread,$(SERVER_LIB_SRCS))
	@echo "==> Linking thread sanitizer server: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_SANITIZER_THREAD) $(LDFLAGS_SANITIZER_THREAD) -o $@ $^ $(LIBS)

$(TARGET_SERVER_SANITIZER_LEAK): $(call GET_OBJS,sanitize_leak,$(SERVER_SRCS)) $(call GET_OBJS,sanitize_leak,$(COMMON_LIB_SRCS)) $(call GET_OBJS,sanitize_leak,$(SERVER_LIB_SRCS))
	@echo "==> Linking leak sanitizer server: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_SANITIZER_LEAK) $(LDFLAGS_SANITIZER_LEAK) -o $@ $^ $(LIBS)

# --- Generic Compilation Rules ---
$(OBJ_DIR)/release/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_RELEASE) -c $< -o $@

$(OBJ_DIR)/debug/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_DEBUG) -c $< -o $@

# FIX: Add new compilation rule for perflog objects
$(OBJ_DIR)/perflog/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_PERFLOG) -c $< -o $@

$(OBJ_DIR)/sanitize_address/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_SANITIZER_ADDRESS) -c $< -o $@

$(OBJ_DIR)/sanitize_thread/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_SANITIZER_THREAD) -c $< -o $@

$(OBJ_DIR)/sanitize_leak/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_SANITIZER_LEAK) -c $< -o $@
