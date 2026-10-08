cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aitsm_sparkplug_test)

set(AITSM_APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../app)

target_include_directories(app PRIVATE ${AITSM_APP_DIR}/include ${AITSM_APP_DIR}/proto)
target_sources(app PRIVATE
	src/main.c
	${AITSM_APP_DIR}/src/sparkplug.c
	${AITSM_APP_DIR}/proto/sparkplug_b.pb.c
)
