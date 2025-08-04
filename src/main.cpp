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

//define you API endpoints here

int main() {
    try {
        util::log::info("Application starting...");
        
        server s;
        
        //register your API endpoints here
                
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
