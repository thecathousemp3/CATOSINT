cmake_minimum_required(VERSION 3.20)

project(cat_osint
    VERSION 0.1.0
    DESCRIPTION "CAT OSINT - local-first public information analysis toolkit"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)
FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cpp_httplib)

add_library(cat_osint_core
    src/core/engine.cpp
    src/connectors/json_api_connector.cpp
    src/models/openai_compatible_model.cpp
    src/models/local_model_discovery.cpp
    src/net/http_client.cpp
    src/api/local_api_server.cpp
)
target_include_directories(cat_osint_core PUBLIC include)
target_link_libraries(cat_osint_core PUBLIC nlohmann_json::nlohmann_json httplib::httplib)

if(WIN32)
    target_link_libraries(cat_osint_core PRIVATE winhttp)
endif()

add_executable(cat-osint src/app/main.cpp)
target_link_libraries(cat-osint PRIVATE cat_osint_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(cat_osint_engine_tests tests/engine_tests.cpp)
    target_link_libraries(cat_osint_engine_tests PRIVATE cat_osint_core)
    add_test(NAME cat_osint_engine_tests COMMAND cat_osint_engine_tests)
endif()

if(MSVC)
    target_compile_options(cat_osint_core PRIVATE /W4 /permissive-)
    target_compile_options(cat-osint PRIVATE /W4 /permissive-)
    if(BUILD_TESTING)
        target_compile_options(cat_osint_engine_tests PRIVATE /W4 /permissive-)
    endif()
else()
    target_compile_options(cat_osint_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(cat-osint PRIVATE -Wall -Wextra -Wpedantic)
    if(BUILD_TESTING)
        target_compile_options(cat_osint_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
