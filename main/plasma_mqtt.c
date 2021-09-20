#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_heap_trace.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "esp_sleep.h"

#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "bkg_mqtt.h"
#define GPIO_LED GPIO_NUM_2
#define GPIO_BUTTON GPIO_NUM_34
static const char *TAG = "PLASMA_MQTT";
extern const uint8_t client_cert_pem_start[] asm("_binary_client_crt_start");
extern const uint8_t client_cert_pem_end[] asm("_binary_client_crt_end");
extern const uint8_t client_key_pem_start[] asm("_binary_client_key_start");
extern const uint8_t client_key_pem_end[] asm("_binary_client_key_end");
extern const uint8_t CA_cert_pem_start[] asm("_binary_CA_crt_start");
extern const uint8_t CA_cert_pem_end[] asm("_binary_CA_crt_end");

#define STRINGIFY(x) #x
#define CAT3_STR(A,B,C) A B C

#define THING_NAME "plasma_lamp"
#define GET_ACCEPTED CAT3_STR("$aws/things/",THING_NAME,"/shadow/get/accepted")
/* update/accepted might be easier on MQTT message lenght restrictions!!!! */
#define UPDATE_TOPIC CAT3_STR("$aws/things/",THING_NAME,"/shadow/update/documents")

esp_mqtt_client_handle_t client_global = 0L;
#define NETWORK_RESTART()						xTaskCreate(network_restart_task, "Network Restart", 4096, NULL, 10, NULL)

static void network_restart_task(void* arg) {
	ESP_LOGI(TAG,"Network Restart");
	// DOESN'T WORK??? mqtt_restart();
	ESP_LOGI(TAG,"Finishing Network Restart");
	vTaskDelete(NULL);
}


void mqtt_app_start(void);
static void network_restart_task(void* arg);

void mqtt_report_powerState(bool powerState, unsigned short requestFlags) {
	char t[90];
	char *temp = t;
	temp += sprintf(temp,"{\"state\":{\"reported\":{\"power\":\"%s\"}}",powerState? "ON":"OFF");
	if (requestFlags & REPORTFLAG_DESIRED) temp += sprintf(temp,",{\"desired\":{\"power\":\"%s\"}}",powerState? "ON":"OFF");
	temp += sprintf(temp,"}");
	ESP_LOGI(TAG, "power change - report: %s",t);
	if (client_global)
		esp_mqtt_client_publish(client_global, CAT3_STR("$aws/things/" ,THING_NAME, "/shadow/update"), t, 0, 0, 0);
}
void mqtt_report_displayMode(short displayMode, unsigned short requestFlags) {
	char t[90];
	char *temp = t;
	temp += sprintf(temp,"{\"state\":{\"reported\": { \"mode\": %d }}",displayMode);
	if (requestFlags & REPORTFLAG_DESIRED) temp += sprintf(temp,",{\"desired\": { \"mode\": %d }}",displayMode);
	temp += sprintf(temp,"}");
	ESP_LOGI(TAG, "mode change - report: %s",t);
	if (client_global)
		esp_mqtt_client_publish(client_global, CAT3_STR("$aws/things/" ,THING_NAME, "/shadow/update"), t, 0, 0, 0);
}
static void got_msg(char *topic, int len, char *data) {
	unsigned short reportFlag=0;
  	ESP_LOGI(TAG,"Got Topic %.*s DIFF=%d\r\n", len, topic,len);
	
	if (!memcmp(topic,GET_ACCEPTED,len)) {
			// On bootup read the last state of things
			cJSON *root = 0L;
			cJSON *lastpower = 0L;
      		ESP_LOGI(TAG, "Got last shadow response");

			root = cJSON_Parse(data);
			if (!root) return;
			cJSON *state = cJSON_GetObjectItem(root,"state");
			if (!state) goto out;
			cJSON *desired = cJSON_GetObjectItem(state,"desired");
			cJSON *reported = cJSON_GetObjectItem(state,"reported");

			if (desired) {
				cJSON *power = cJSON_GetObjectItem(desired,"power");
				if (reported) {
						cJSON *lastpower = cJSON_GetObjectItem(desired,"power");
						if (lastpower)
							ESP_LOGI(TAG, "Power had been reported as %s",power->valuestring);
						if (lastpower && strcmp(lastpower->valuestring,power->valuestring)) {
							reportFlag = REPORTFLAG_NOMQTT;
						}
					}
				if (power && power->valuestring) {
					/* Avoid reporting if we've desired already equals reported */
					ESP_LOGI(TAG, "Power Desired to be  %s",power->valuestring);
					if (lastpower && !strcmp(lastpower->valuestring,power->valuestring))
						reportFlag |= REPORTFLAG_NOMQTT;
					if (!strcmp(power->valuestring,"ON")) {
						//gpio_set_level(GPIO_RELAY1,1); 
						//gpio_set_level(GPIO_LED,1); 
						//relay1state=1;
						plasma_powerOn(reportFlag);
					} else {
						//gpio_set_level(GPIO_RELAY1,0); 
						//gpio_set_level(GPIO_LED,0); 
						//relay1state=0;
						plasma_powerOff(reportFlag);
					}

				}
			}

out:
			ESP_LOGI(TAG, "CP %d",__LINE__);
		if (root) cJSON_Delete(root); // Delete entire TREE

	} else if (!memcmp(topic,UPDATE_TOPIC,len)) {
			// Process an update
            ESP_LOGI(TAG, "Got shadow update");
						// UPDATE will give us a current/state/desired
						// GET_ACCEPTED will give us a state/desired
			
						cJSON *root = cJSON_Parse(data);
						if (!root) return;

						
						cJSON *current = cJSON_GetObjectItem(root,"current");
						if (!current) goto out2;
						cJSON *state = cJSON_GetObjectItem(current,"state");
						if (!state) goto out2;
						cJSON *desired = cJSON_GetObjectItem(state,"desired");
						cJSON *reported = cJSON_GetObjectItem(state,"reported");
						if (desired) {
							/* Power */
							cJSON *power = cJSON_GetObjectItem(desired,"power");
							cJSON *were = 0L;
							if (reported) were = cJSON_GetObjectItem(reported,"power");
							if (power && were && !strcmp(were->valuestring,power->valuestring))
								reportFlag |= REPORTFLAG_NOMQTT;

							if (power && power->valuestring) {
								ESP_LOGI(TAG, "Set Power to %s",power->valuestring);
								if (!strcmp(power->valuestring,"ON")) {
									plasma_powerOn(reportFlag);
								} else {
									plasma_powerOff(reportFlag);
								}
							}
							/* Mode */
							reportFlag = 0;
							cJSON *mode = cJSON_GetObjectItem(desired,"mode");
							were = 0L;
							if (reported) were = cJSON_GetObjectItem(reported,"mode");
							if (mode && were && were->valueint==mode->valueint)
								reportFlag |= REPORTFLAG_NOMQTT;

							if (mode) {
								ESP_LOGI(TAG, "Set Mode to %d",mode->valueint);
								change_displayMode(mode->valueint,reportFlag);
								
							}
						}

out2:
						cJSON_Delete(root); // Delete entire TREE
			}
			ESP_LOGI(TAG, "CP %d",__LINE__);
}


static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;

    // your_context_t *context = event->context;

	gpio_set_level(GPIO_LED,!gpio_get_level(GPIO_LED));
	vTaskDelay(100);
	gpio_set_level(GPIO_LED,!gpio_get_level(GPIO_LED));
    
	
	switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            int msg_id;
            msg_id = esp_mqtt_client_subscribe(client,CAT3_STR( "$aws/things/", THING_NAME,"/shadow/get/accepted"), 0);
            msg_id = esp_mqtt_client_subscribe(client, CAT3_STR("$aws/things/", THING_NAME, "/shadow/update/documents"), 0);
            //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);


            //msg_id = esp_mqtt_client_subscribe(client, "relay1", 0);
            //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            //msg_id = esp_mqtt_client_subscribe(client, "#", 0);
            //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            //msg_id = esp_mqtt_client_subscribe(client, "#", 3);
            //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            //msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
            //ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
            break;


        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
						esp_wifi_disconnect();
						printf("Wifi stop\n");
						esp_wifi_stop();
						printf("Entering deep sleep\n");
						ESP_LOGI(TAG, "Entering deep sleep\n");
						esp_deep_sleep(3000000LL);
						//mqtt_restart();
			
						NETWORK_RESTART();
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
						// Get the shadow
            msg_id = esp_mqtt_client_publish(client, CAT3_STR("$aws/things/", THING_NAME, "/shadow/get"), "", 0, 0, 0);
            //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
						//sleep_then_reboot();
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA offset %d total %u",event->current_data_offset,event->total_data_len);
            if (event->current_data_offset > 0) {
							ESP_LOGE(TAG, "MQTT TRUNCATED - INCREASE MAX LEN");
						}
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
						if (event->current_data_offset == 0) {
							got_msg(event->topic,event->topic_len,event->data);
							}
            break;
		case MQTT_EVENT_BEFORE_CONNECT:
			ESP_LOGI(TAG,"MQTT_EVENT_BEFORE_CONNECT");
			break;
		//case MQTT_EVENT_DELETED:
		//	ESP_LOGI(TAG,"MQTT_EVENT_DELETED");
		//	break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR last_tls=%d tls_stack=%d tls_flags=0x%x err_type=%d conn_rc=%d ",
				event->error_handle->esp_tls_last_esp_err,
				event->error_handle->esp_tls_stack_err,
				event->error_handle->esp_tls_cert_verify_flags,
				event->error_handle->error_type,
				event->error_handle->connect_return_code
				);
						//mqtt_restart();
						// Cannot be stopped inside MQTT task - schedule elsewhere??
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        //.uri = "mqtts://test.mosquitto.org:8884",
        .uri = "mqtts://a1g918rz1nkte8-ats.iot.us-east-1.amazonaws.com:8883",
        .event_handle = mqtt_event_handler,
		.client_id = THING_NAME, // Required for iot:connect client/POOL_SHED_RELAY
        .client_cert_pem = (const char *)client_cert_pem_start,
        .client_key_pem = (const char *)client_key_pem_start,
				.cert_pem = (const char *) CA_cert_pem_start,
				.buffer_size=1500
    };

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
	esp_err_t err;
    ESP_ERROR_CHECK( esp_mqtt_client_start(client));
	
		client_global=client;
}