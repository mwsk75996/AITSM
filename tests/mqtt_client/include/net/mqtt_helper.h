/* Minimal NCS MQTT-helper interface for a native, controllable fake backend. */
#ifndef TEST_MQTT_HELPER_H_
#define TEST_MQTT_HELPER_H_
#include <zephyr/net/mqtt.h>
enum mqtt_helper_error { MQTT_HELPER_ERROR_MSG_SIZE };
struct mqtt_helper_buf { char *ptr; size_t size; };
struct mqtt_helper_conn_params {
	struct mqtt_helper_buf hostname, device_id, user_name, password;
	const char *if_name;
};
struct mqtt_helper_cfg {
	struct {
		void (*on_connack)(enum mqtt_conn_return_code, bool);
		void (*on_disconnect)(int);
		void (*on_puback)(uint16_t, int);
		void (*on_error)(enum mqtt_helper_error);
	} cb;
};
int mqtt_helper_init(struct mqtt_helper_cfg *cfg);
int mqtt_helper_connect(struct mqtt_helper_conn_params *params);
int mqtt_helper_publish(const struct mqtt_publish_param *param);
uint16_t mqtt_helper_msg_id_get(void);
#endif
