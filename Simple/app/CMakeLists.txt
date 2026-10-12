cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aitsm_simple)

set(AITSM_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${AITSM_GENERATED_DIR})

# The MQTT credentials come from the build environment. They are written only
# to the ignored build directory and are never stored in the repository.
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
	${CMAKE_CURRENT_SOURCE_DIR}/src/mqtt_credentials.h.in
	${AITSM_GENERATED_DIR}/mqtt_credentials.h
	@ONLY
)

# Convert the CA certificate to a byte list that mqtt_client.c includes.
generate_inc_file_for_target(
	app
	${CMAKE_CURRENT_SOURCE_DIR}/certs/ca-cert.pem
	${AITSM_GENERATED_DIR}/ca-cert.pem
)

target_include_directories(app PRIVATE proto ${AITSM_GENERATED_DIR})
target_sources(app PRIVATE
	src/main.c
	src/network.c
	src/mqtt_client.c
	src/sensor.c
	src/batch.c
	src/sparkplug.c
	proto/sparkplug_b.pb.c
)
