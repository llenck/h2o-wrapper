cmake_minimum_required(VERSION 3.0)

project(lh2ow)

# stuff for compiling and linking with external projects
include_directories(
	deps/h2o/include
	deps/h2o/deps/cloexec
	deps/h2o/deps/brotli/c/include
	deps/h2o/deps/golombset
	deps/h2o/deps/hiredis
	deps/h2o/deps/libgkc
	deps/h2o/deps/libyrmcds
	deps/h2o/deps/klib
	deps/h2o/deps/neverbleed
	deps/h2o/deps/picohttpparser
	deps/h2o/deps/picotest
	deps/h2o/deps/picotls/deps/cifra/src/ext
	deps/h2o/deps/picotls/deps/cifra/src
	deps/h2o/deps/picotls/deps/micro-ecc
	deps/h2o/deps/picotls/include
	deps/h2o/deps/quicly/include
	deps/h2o/deps/yaml/include
	deps/h2o/deps/yoml

	# the headers of unicos dependencies are copied into its include dir
	deps/unico/include
)

include(ExternalProject)

ExternalProject_Add(h2o
	SOURCE_DIR deps/h2o
	PREFIX deps/h2o
	BINARY_DIR deps/h2o
	STEP_TARGETS build
	BUILD_COMMAND cmake --build . --target libh2o
	INSTALL_COMMAND cmake -E echo "Skipping install step"
)
ExternalProject_Add(unico
	SOURCE_DIR deps/unico
	PREFIX deps/unico
	BINARY_DIR deps/unico
	STEP_TARGETS build
	CONFIGURE_COMMAND cmake -E echo "Skipping configure step"
	BUILD_COMMAND ./compile
	INSTALL_COMMAND cmake -E echo "Skipping install step"
)

# stuff for building our own library
set(CMAKE_C_FLAGS "-Wall -Wextra -O3")
set(CMAKE_C_FLAGS_DEBUG "-Wall -Wextra -Wpedantic -Werror -Og -g")

include_directories(include)
add_library(h2ow-pre STATIC lib/runtime.c lib/settings.c lib/handlers.c lib/run-setup.c lib/utils.c)

# combine our library with others so people don't have to link against them
add_custom_target(h2ow ALL
	COMMAND ./combine-libs
	DEPENDS h2ow-pre h2o unico
)
