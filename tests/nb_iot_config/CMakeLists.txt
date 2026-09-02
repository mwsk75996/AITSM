cmake_minimum_required(VERSION 3.20.0)

# Build with the application's real prj.conf so the asserts below verify the
# actual configuration instead of a copy that could drift out of sync.
set(AITSM_APP_DIR ${CMAKE_CURRENT_LIST_DIR}/../../app)
set(CONF_FILE ${AITSM_APP_DIR}/prj.conf)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aitsm_nb_iot_config_test)

target_include_directories(app PRIVATE ${AITSM_APP_DIR}/include)
target_sources(app PRIVATE
	${AITSM_APP_DIR}/src/main.c
	${AITSM_APP_DIR}/src/app_controller.c
	${AITSM_APP_DIR}/src/data_transmission.c
	${AITSM_APP_DIR}/src/led_status.c
	${AITSM_APP_DIR}/src/mqtt_client.c
	${AITSM_APP_DIR}/src/mqtt_credentials_provision.c
	${AITSM_APP_DIR}/src/network.c
	src/config_asserts.c
)
