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
#include "esp_log.h"
#include "esp_console.h"

#include "data.h"

static const char *TAG = "http";

/* An HTTP GET handler */
static esp_err_t hello_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-2") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Test-Header-2", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Test-Header-2: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-1") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Test-Header-1", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Test-Header-1: %s", buf);
        }
        free(buf);
    }

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[32];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "query1", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query1=%s", param);
            }
            if (httpd_query_key_value(buf, "query3", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query3=%s", param);
            }
            if (httpd_query_key_value(buf, "query2", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query2=%s", param);
            }
        }
        free(buf);
    }

    /* Set some custom headers */
    httpd_resp_set_hdr(req, "Custom-Header-1", "Custom-Value-1");
    httpd_resp_set_hdr(req, "Custom-Header-2", "Custom-Value-2");

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    const char* resp_str = (const char*) req->user_ctx;
    httpd_resp_send(req, resp_str, strlen(resp_str));

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}

static const httpd_uri_t hello = {
    .uri       = "/hello",
    .method    = HTTP_GET,
    .handler   = hello_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = "Hello World!"
};

static esp_err_t index_get_handler(httpd_req_t *req)
{
    extern const unsigned char index_start[] asm("_binary_index_html_start");
    extern const unsigned char index_end[]   asm("_binary_index_html_end");
    const size_t index_size = (index_end - index_start);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_start, index_size);
    return ESP_OK;
}

// A "regress" buffer has kv pairs
char *find_regress(char *key, char *buf) {
        while (*buf) {
          //printf("%s = ",buf);
          if (!strcmp(key,buf)) {
                  return (&buf[strlen(buf)+1]);
          }
          buf+= strlen(buf)+1;
          //printf("%s\n",buf);
          buf+= strlen(buf)+1;
        }
    return 0L;
}

#define MAX_PAYLOAD 512
static esp_err_t index_post_handler(httpd_req_t *req) {
    char *buf=malloc(MAX_PAYLOAD);
    char *t,*tt;
    char *e,*ee;
    int i;
    
	if (!buf)
		ESP_ERROR_CHECK(1);
    int ret, remaining = req->content_len;

	if (req->content_len > MAX_PAYLOAD-2)
		ESP_LOGE(TAG,"Max Payload excceded");
    while (remaining > 0) {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, buf,
                        MIN(remaining, MAX_PAYLOAD-2))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }
            return ESP_FAIL;
        }

        buf[ret]=(char)0;
        buf[ret+1]=(char)0;
        /* Send back the same data */
        remaining -= ret;

        /* Log data received */
        ESP_LOGI(TAG, "=========== RECEIVED DATA %d==========",req->content_len);
        ESP_LOGI(TAG, "%.*s", ret, buf);
        ESP_LOGI(TAG, "====================================");
        t =strtok_r(buf,"&",&tt);
        while (t) {
                printf("%s\n",t);
                e =strtok_r(t,"=",&ee);
                if (e) {
                        e =strtok_r(0L,"=",&ee);
                        printf("  %s = %s\n",t,e);
                }
                t =strtok_r(0L,"&",&tt);
        }
        // Try my "regress"
        t=buf;
        printf("\nREGRESS\n");
        while (*t) {
          printf("%s = ",t);
          t+= strlen(t)+1;
          printf("%s\n",t);
          t+= strlen(t)+1;
        }
        printf("REGRESS DONE\n\n");

        if (find_regress("Save_Preset",buf)) {
            nvs_stats_t nvs_stats;
            esp_err_t err;
            nvs_handle_t my_handle;
            unsigned slot;
            char slotname[10];
            slot = strtoul(find_regress("save_slot",buf),0L,0);
            preset_t *p = malloc(sizeof(preset_t));
            memcpy(&p->rings,&rings,sizeof(rings));
            p->sparkle = sparkle;
            p->numflicker = numflicker;
            p->flicker_r = flicker_r;
            p->flicker_g = flicker_g;
            p->flicker_b = flicker_b;
            sprintf(slotname,"preset%d",slot);
            nvs_get_stats(NULL, &nvs_stats);
            printf("BEFORE Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
                nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
            err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
            ESP_ERROR_CHECK(err);
            err = nvs_set_blob(my_handle,slotname,p,sizeof(preset_t));
            ESP_ERROR_CHECK(nvs_commit(my_handle));
            nvs_close(my_handle);
            free(p);
            nvs_get_stats(NULL, &nvs_stats);
            printf("BEFORE Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
                nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
        }
        else if (find_regress("Load_Preset",buf)) {
            unsigned slot;
            slot = strtoul(find_regress("save_slot",buf),0L,0);
            load_preset(slot);
        } else if (find_regress("Hold",buf)) {
            next_time=0;
        } else if (find_regress("Next_Preset",buf)) {
            //advance_slot();
            fade_out=512;
        } else if (find_regress("power_on",buf)) {
              plasma_powerOn(0);
        } else if (find_regress("power_off",buf)) {
              plasma_powerOff(0);
        }
        else if (find_regress("Set",buf)) {
                if (find_regress("sparkle_change",buf)) {
                    char *s;
                  s=find_regress("sparkle",buf);
                  printf("sparkle now %s\n",s);
                  sparkle = strtoul(s,0L,0);
                }
                if (find_regress("numflicker_change",buf)) {
                    char *s;
                  s=find_regress("numflicker",buf);
                  printf("numflicker now %s\n",s);
                  numflicker = strtoul(s,0L,0);
                }
                if (find_regress("flicker_change",buf)) {
                    char *s;
                  unsigned long rgb;
                  s=find_regress("flicker",buf);
                  rgb = strtoul(&s[3],0L,16);
                  flicker_r = rgb >> 16;
                  flicker_g = (rgb >> 8) & 0xff;
                  flicker_b = rgb & 0xff;
                  printf("flicker now %s %lx\n",&s[3],rgb);
                }
                for (i=0;i<RINGS;i++) {
                    char *s;
                    char rr[16];
                    sprintf(rr,"ring%d",i);
                    if (find_regress(rr,buf)) {
                      printf("Set ring %d\n",i);
                        if (find_regress("ring_huespeed_change",buf)) {
                          s=find_regress("ring_huespeed",buf);
                          rings[i].huespeed = strtoul(s,0L,0)<<8;
                          printf("huespeed %d now %lx\n",i,rings[i].huespeed);
                        }
                        if (find_regress("ring_color_change",buf)) {
                          unsigned long rgb;
                          s=find_regress("ring_color",buf);
                          rgb = strtoul(&s[3],0L,16);
                          rings[i].red = rgb >> 16;
                          rings[i].green = (rgb >> 8) & 0xff;
                          rings[i].blue = rgb & 0xff;
                          printf("color %d now %s %lx\n",i,&s[3],rgb);
                        }
                        if (find_regress("ring_width_change",buf)) {
                          s=find_regress("ring_width",buf);
                          rings[i].width = strtoul(s,0L,0);
                          printf("width %d now %s\n",i,s);
                        }
                        if (find_regress("ring_length_change",buf)) {
                          s=find_regress("ring_length",buf);
                          rings[i].len = strtoul(s,0L,0);
                          printf("length %d now %s\n",i,s);
                        }
                        if (find_regress("ring_mode_change",buf)) {
                          s=find_regress("ring_mode",buf);
                          rings[i].mode = strtoul(s,0L,0);
                          printf("mode %d now %s\n",i,s);
                        }
                        if (find_regress("ring_speed_change",buf)) {
                          float f;
                          s=find_regress("ring_speed",buf);
                          f= strtof(s,0L);
                          rings[i].speed= f;
                          printf("speed %d now %f\n",i,f);
                        }
                        if (find_regress("ring_angle_change",buf)) {
                          s=find_regress("ring_angle",buf);
                          rings[i].angle= strtoul(s,0L,0);
                          printf("angle %d now %s\n",i,s);
                        }
                    }
              }
         }

    }
	ESP_LOGI(TAG,"Index post");
          free(buf);
	return (index_get_handler(req));
}

static const httpd_uri_t index_uri = {
    .uri       = "/index.html",
    .method    = HTTP_GET,
    .handler   = index_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = "Lava Lamp"
};

static const httpd_uri_t index_post_uri = {
    .uri       = "/index.html",
    .method    = HTTP_POST,
    .handler   = index_post_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = "Lava Lamp"
};
/* An HTTP POST handler */
static esp_err_t echo_post_handler(httpd_req_t *req)
{
    char buf[100];
    int ret, remaining = req->content_len;

    while (remaining > 0) {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, buf,
                        MIN(remaining, sizeof(buf)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }
            return ESP_FAIL;
        }

        /* Send back the same data */
        httpd_resp_send_chunk(req, buf, ret);
        remaining -= ret;

        /* Log data received */
        ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        ESP_LOGI(TAG, "%.*s", ret, buf);
        ESP_LOGI(TAG, "====================================");
    }

    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t echo = {
    .uri       = "/echo",
    .method    = HTTP_POST,
    .handler   = echo_post_handler,
    .user_ctx  = NULL
};

/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/hello", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/hello URI is not available");
        /* Return ESP_OK to keep underlying socket open */
        return ESP_OK;
    } else if (strcmp("/echo", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
        /* Return ESP_FAIL to close underlying socket */
        return ESP_FAIL;
    }
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

/* An HTTP PUT handler. This demonstrates realtime
 * registration and deregistration of URI handlers
 */
static esp_err_t ctrl_put_handler(httpd_req_t *req)
{
    char buf;
    int ret;

    if ((ret = httpd_req_recv(req, &buf, 1)) <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    if (buf == '0') {
        /* URI handlers can be unregistered using the uri string */
        ESP_LOGI(TAG, "Unregistering /hello and /echo URIs");
        httpd_unregister_uri(req->handle, "/hello");
        httpd_unregister_uri(req->handle, "/echo");
        /* Register the custom error handler */
        httpd_register_err_handler(req->handle, HTTPD_404_NOT_FOUND, http_404_error_handler);
    }
    else {
        ESP_LOGI(TAG, "Registering /hello and /echo URIs");
        httpd_register_uri_handler(req->handle, &hello);
        httpd_register_uri_handler(req->handle, &echo);
        /* Unregister custom error handler */
        httpd_register_err_handler(req->handle, HTTPD_404_NOT_FOUND, NULL);
    }

    /* Respond with empty body */
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t ctrl = {
    .uri       = "/ctrl",
    .method    = HTTP_PUT,
    .handler   = ctrl_put_handler,
    .user_ctx  = NULL
};

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.core_id=0;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &hello);
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &index_post_uri);
        httpd_register_uri_handler(server, &echo);
        httpd_register_uri_handler(server, &ctrl);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

void disconnect_handler(void* arg, esp_event_base_t event_base, 
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
    }
}

void connect_handler(void* arg, esp_event_base_t event_base, 
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}

