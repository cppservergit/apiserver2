# Compiler
CXX = g++

# --- Build Configurations ---
.DEFAULT_GOAL := help
CXXFLAGS_BASE = -std=c++23 -Wall -Wextra

CXXFLAGS_DEBUG = -g -DENABLE_DEBUG_LOGS -DUSE_STACKTRACE
CXXFLAGS_RELEASE = -O2 -march=native -DNDEBUG -flto=4
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
TARGET_TESTRUNNER_RELEASE = test_runner
TARGET_TESTRUNNER_DEBUG = test_runner_debug
TARGET_TESTRUNNER_SANITIZER_ADDRESS = test_runner_sanitizer_address
TARGET_TESTRUNNER_SANITIZER_THREAD = test_runner_sanitizer_thread
TARGET_TESTRUNNER_SANITIZER_LEAK = test_runner_sanitizer_leak

TARGET_SERVER_RELEASE = server_app
TARGET_SERVER_DEBUG = server_debug_app
TARGET_SERVER_SANITIZER_ADDRESS = server_sanitizer_address_app
TARGET_SERVER_SANITIZER_THREAD = server_sanitizer_thread_app
TARGET_SERVER_SANITIZER_LEAK = server_sanitizer_leak_app

# --- Source File Lists ---
TESTRUNNER_SRCS = main.cpp
SERVER_SRCS = test_server.cpp
COMMON_LIB_SRCS = http_request.cpp json_parser.cpp pkeyutil.cpp sql.cpp jwt.cpp
SERVER_LIB_SRCS = server.cpp

# --- Object File Definitions ---
define GET_OBJS
$(patsubst %.cpp,$(OBJ_DIR)/$(1)/%.o,$(2))
endef

# --- Libraries to Link ---
LIBS = -ljson-c -lcrypto -lstdc++exp -lbacktrace -lodbc -luuid -ltbb # Added -ltbb

# --- User-Facing Commands ---
.PHONY: all release debug server run run_server clean help \
		sanitize_address sanitize_thread sanitize_leak \
		run_sanitizer_address run_sanitizer_thread run_sanitizer_leak \
		server_debug server_sanitize_address server_sanitize_thread server_sanitize_leak \
		run_server_debug run_server_sanitizer_address run_server_sanitizer_thread run_server_sanitizer_leak

all: release

release:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_TESTRUNNER_RELEASE)

debug:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_TESTRUNNER_DEBUG)

server:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_SERVER_RELEASE)

server_debug:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_SERVER_DEBUG)

sanitize_address:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_TESTRUNNER_SANITIZER_ADDRESS)

sanitize_thread:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_TESTRUNNER_SANITIZER_THREAD)

sanitize_leak:
	@clear
	@$(MAKE) --no-print-directory $(TARGET_TESTRUNNER_SANITIZER_LEAK)

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
	./$(TARGET_TESTRUNNER_RELEASE)

run_server: server
	./$(TARGET_SERVER_RELEASE)

run_server_debug: server_debug
	./$(TARGET_SERVER_DEBUG)

run_sanitizer_address: sanitize_address
	./$(TARGET_TESTRUNNER_SANITIZER_ADDRESS)

run_sanitizer_thread: sanitize_thread
	./$(TARGET_TESTRUNNER_SANITIZER_THREAD)

run_sanitizer_leak: sanitize_leak
	./$(TARGET_TESTRUNNER_SANITIZER_LEAK)

run_server_sanitizer_address: server_sanitize_address
	./$(TARGET_SERVER_SANITIZER_ADDRESS)

run_server_sanitizer_thread: server_sanitize_thread
	./$(TARGET_SERVER_SANITIZER_THREAD)

run_server_sanitizer_leak: server_sanitize_leak
	./$(TARGET_SERVER_SANITIZER_LEAK)

clean:
	@echo "==> Cleaning project..."
	rm -rf $(OBJ_DIR) $(wildcard *_app) $(wildcard test_runner*)

help:
	@clear
	@echo "Usage: make [target]"
	@echo ""
	@echo "Test Runner Targets:"
	@echo "  release (default)    Build the release test runner."
	@echo "  run                  Build and run the release test runner."
	@echo "  debug                Build the debug test runner."
	@echo "  sanitize_address     Build test runner with AddressSanitizer."
	@echo "  sanitize_thread      Build test runner with ThreadSanitizer."
	@echo "  sanitize_leak        Build test runner with LeakSanitizer."
	@echo ""
	@echo "Server Targets:"
	@echo "  server               Build the release server."
	@echo "  run_server           Build and run the release server."
	@echo "  server_debug         Build the debug server."
	@echo "  run_server_debug     Build and run the debug server."
	@echo "  server_sanitize_address Build server with AddressSanitizer."
	@echo "  server_sanitize_thread Build server with ThreadSanitizer."
	@echo "  server_sanitize_leak Build server with LeakSanitizer."
	@echo ""
	@echo "Other Targets:"
	@echo "  clean                Remove all build artifacts."


# --- Linking Rules (for build artifacts) ---
$(TARGET_TESTRUNNER_RELEASE): $(call GET_OBJS,release,$(TESTRUNNER_SRCS)) $(call GET_OBJS,release,$(COMMON_LIB_SRCS))
	@echo "==> Linking release test runner: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_RELEASE) -o $@ $^ $(LIBS)
	@echo "==> Stripping symbols..."
	strip $@

$(TARGET_TESTRUNNER_DEBUG): $(call GET_OBJS,debug,$(TESTRUNNER_SRCS)) $(call GET_OBJS,debug,$(COMMON_LIB_SRCS))
	@echo "==> Linking debug test runner: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_DEBUG) -o $@ $^ $(LIBS)

$(TARGET_SERVER_RELEASE): $(call GET_OBJS,release,$(SERVER_SRCS)) $(call GET_OBJS,release,$(COMMON_LIB_SRCS)) $(call GET_OBJS,release,$(SERVER_LIB_SRCS))
	@echo "==> Linking release server: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_RELEASE) -o $@ $^ $(LIBS)
	@echo "==> Stripping symbols..."
	strip $@

$(TARGET_SERVER_DEBUG): $(call GET_OBJS,debug,$(SERVER_SRCS)) $(call GET_OBJS,debug,$(COMMON_LIB_SRCS)) $(call GET_OBJS,debug,$(SERVER_LIB_SRCS))
	@echo "==> Linking debug server: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_DEBUG) -o $@ $^ $(LIBS)

$(TARGET_TESTRUNNER_SANITIZER_ADDRESS): $(call GET_OBJS,sanitize_address,$(TESTRUNNER_SRCS)) $(call GET_OBJS,sanitize_address,$(COMMON_LIB_SRCS))
	@echo "==> Linking address sanitizer test runner: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_SANITIZER_ADDRESS) $(LDFLAGS_SANITIZER_ADDRESS) -o $@ $^ $(LIBS)

$(TARGET_SERVER_SANITIZER_ADDRESS): $(call GET_OBJS,sanitize_address,$(SERVER_SRCS)) $(call GET_OBJS,sanitize_address,$(COMMON_LIB_SRCS)) $(call GET_OBJS,sanitize_address,$(SERVER_LIB_SRCS))
	@echo "==> Linking address sanitizer server: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_SANITIZER_ADDRESS) $(LDFLAGS_SANITIZER_ADDRESS) -o $@ $^ $(LIBS)

$(TARGET_TESTRUNNER_SANITIZER_THREAD): $(call GET_OBJS,sanitize_thread,$(TESTRUNNER_SRCS)) $(call GET_OBJS,sanitize_thread,$(COMMON_LIB_SRCS))
	@echo "==> Linking thread sanitizer test runner: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_SANITIZER_THREAD) $(LDFLAGS_SANITIZER_THREAD) -o $@ $^ $(LIBS)

$(TARGET_SERVER_SANITIZER_THREAD): $(call GET_OBJS,sanitize_thread,$(SERVER_SRCS)) $(call GET_OBJS,sanitize_thread,$(COMMON_LIB_SRCS)) $(call GET_OBJS,sanitize_thread,$(SERVER_LIB_SRCS))
	@echo "==> Linking thread sanitizer server: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_SANITIZER_THREAD) $(LDFLAGS_SANITIZER_THREAD) -o $@ $^ $(LIBS)

$(TARGET_TESTRUNNER_SANITIZER_LEAK): $(call GET_OBJS,sanitize_leak,$(TESTRUNNER_SRCS)) $(call GET_OBJS,sanitize_leak,$(COMMON_LIB_SRCS))
	@echo "==> Linking leak sanitizer test runner: $@"
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_SANITIZER_LEAK) $(LDFLAGS_SANITIZER_LEAK) -o $@ $^ $(LIBS)

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

$(OBJ_DIR)/sanitize_address/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_SANITIZER_ADDRESS) -c $< -o $@

$(OBJ_DIR)/sanitize_thread/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_SANITIZER_THREAD) -c $< -o $@

$(OBJ_DIR)/sanitize_leak/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS_BASE) $(CXXFLAGS_SANITIZER_LEAK) -c $< -o $@
