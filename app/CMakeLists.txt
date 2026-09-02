cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aitsm_cellular_connection)

set(AITSM_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${AITSM_GENERATED_INCLUDE_DIR})

# Credentials are supplied by the build environment. They are written only to
# the ignored build directory and are never stored in the repository.
set(AITSM_MQTT_USERNAME "$ENV{AITSM_MQTT_USERNAME}")
set(AITSM_MQTT_PASSWORD "$ENV{AITSM_MQTT_PASSWORD}")
if(AITSM_MQTT_USERNAME STREQUAL "")
	set(AITSM_MQTT_USERNAME "thingy91x")
endif()

string(REPLACE "\\" "\\\\" AITSM_MQTT_USERNAME "${AITSM_MQTT_USERNAME}")
string(REPLACE "\"" "\\\"" AITSM_MQTT_USERNAME "${AITSM_MQTT_USERNAME}")
string(REPLACE "\\" "\\\\" AITSM_MQTT_PASSWORD "${AITSM_MQTT_PASSWORD}")
string(REPLACE "\"" "\\\"" AITSM_MQTT_PASSWORD "${AITSM_MQTT_PASSWORD}")

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/include/mqtt_credentials.h.in
	${AITSM_GENERATED_INCLUDE_DIR}/mqtt_credentials.h
	@ONLY
)

set(AITSM_CERTS_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/certs)
file(MAKE_DIRECTORY ${AITSM_CERTS_BINARY_DIR})
zephyr_include_directories(${AITSM_CERTS_BINARY_DIR})
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/certs/ca-cert.pem)
	generate_inc_file_for_target(
		app
		${CMAKE_CURRENT_SOURCE_DIR}/certs/ca-cert.pem
		${AITSM_CERTS_BINARY_DIR}/ca-cert.pem
	)
endif()

target_include_directories(app PRIVATE include ${AITSM_GENERATED_INCLUDE_DIR})
target_sources(app PRIVATE
	src/main.c
	src/app_controller.c
	src/data_transmission.c
	src/network.c
	src/mqtt_client.c
	src/mqtt_credentials_provision.c
	src/led_status.c
)
