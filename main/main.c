#include 	<stdio.h>
#include	<string.h>
#include	"esp_event.h"	//	for usleep

#include	"neopixel.h"
#include	<esp_log.h>
#include <math.h>

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "freertos/task.h"
#include "protocol_examples_common.h"

#include "mqtt_client.h"
#include <esp_http_server.h>

#include "bkg_mqtt.h"
#define GPIO_INPUT_IO_0    0
#define ESP_INTR_FLAG_DEFAULT 0
//#define	NR_LED		3
#include "esp_log.h"
#include "esp_console.h"

#include "data.h"

static const char *TAG = "PlasmaLamp";


#define GPIO_LED GPIO_NUM_2

static void IRAM_ATTR gpio_isr_handler(void* arg) {
	//printf("INT should go to xQueue\r\n");

  if (++mode==4)
    mode=0;
}

// 0 means "off" - nonzero means "on" - errors default to "off"
bool load_powerState() {
    esp_err_t err;
    unsigned char v;
    nvs_handle_t my_handle;
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    ESP_ERROR_CHECK(err);
    err = nvs_get_u8(my_handle,"powerState",&v);
    if (err != ESP_OK)
      v=0;
    nvs_close(my_handle);
    return (v!=0);
}
short load_displayMode() {
    esp_err_t err;
    unsigned char v;
    nvs_handle_t my_handle;
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    ESP_ERROR_CHECK(err);
    err = nvs_get_u8(my_handle,"displayMode",&v);
    if (err != ESP_OK)
      v=MODE_AUTOADVANCE;
    nvs_close(my_handle);
    return (v);
}

void save_powerState() {
    esp_err_t err;
    nvs_stats_t nvs_stats;
    nvs_handle_t my_handle;
    nvs_get_stats(NULL, &nvs_stats);
    printf("BEFORE Save Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
        nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    ESP_ERROR_CHECK(err);
    err = nvs_set_u8(my_handle,"powerState",powerState? 1 : 0);
    ESP_ERROR_CHECK(nvs_commit(my_handle));
    nvs_close(my_handle);
    nvs_get_stats(NULL, &nvs_stats);
    printf("AFTER save Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
        nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
}
void save_displayMode() {
    esp_err_t err;
    nvs_stats_t nvs_stats;
    nvs_handle_t my_handle;
    nvs_get_stats(NULL, &nvs_stats);
    printf("BEFORE Save Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
        nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    ESP_ERROR_CHECK(err);
    err = nvs_set_u8(my_handle,"displayMode",mode);
    ESP_ERROR_CHECK(nvs_commit(my_handle));
    nvs_close(my_handle);
    nvs_get_stats(NULL, &nvs_stats);
    printf("AFTER save Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
        nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
}
int load_preset(int slot) {
    size_t actualSize;
    esp_err_t err;
    nvs_handle_t my_handle;
    char slotname[10];
    preset_t *p = malloc(sizeof(preset_t));
    sprintf(slotname,"preset%d",slot);
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    ESP_ERROR_CHECK(err);
    err = nvs_get_blob(my_handle,slotname,0L,&actualSize);
    if (err || actualSize != sizeof(preset_t)) {
      ESP_LOGE(TAG,"Error loading preset %d",slot);
      return -1;
    } else {
            err = nvs_get_blob(my_handle,slotname,p,&actualSize);
            memcpy(&rings,&p->rings,sizeof(rings));
            sparkle = p->sparkle;
            numflicker = p->numflicker;
            flicker_r = p->flicker_r;
            flicker_g = p->flicker_g;
            flicker_b = p->flicker_b;
            ESP_LOGI(TAG,"Preset %d loaded",slot);
    }
    free(p);
    nvs_close(my_handle);
    preset_running=slot;
    //time(&next_time);
    //next_time += 10; // Advance to next in 10 seconds
    return 0;
}

extern	void
app_main (void)
{
    static httpd_handle_t server = NULL;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_WIFI
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET

		gpio_set_direction(GPIO_LED,GPIO_MODE_OUTPUT);
		gpio_set_level(GPIO_LED,1);

  		// Set Button handler 
    gpio_config_t io_conf;
		io_conf.intr_type = GPIO_PIN_INTR_NEGEDGE;
    io_conf.pin_bit_mask = 1<< GPIO_INPUT_IO_0;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
    ;
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

    gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void*) GPIO_INPUT_IO_0);
	//test_neopixel();
    
    /* Start the server for the first time */
    server = start_webserver();

    mqtt_app_start();


  xTaskCreatePinnedToCore(&test_neopixel, "Neopixels", 8192, NULL, 55, NULL,1);
  xTaskCreatePinnedToCore(tcp_server_task, "tcp_server", 4096, (void*)0L, 5, NULL,0);
  xTaskCreatePinnedToCore(&console, "Console", 8192, NULL, 1, NULL,0);
}

