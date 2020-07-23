cmake_minimum_required(VERSION 3.0)

project(lh2ow)

set(CMAKE_C_FLAGS "-Wall -Wextra -O3")
set(CMAKE_C_FLAGS_DEBUG "-Wall -Wextra -Wpedantic -Werror -Og -g")

include_directories(include)
add_library(h2ow STATIC lib/runtime.c lib/settings.c lib/handlers.c lib/run-setup.c lib/utils.c)
