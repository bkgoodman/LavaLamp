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
#define	NEOPIXEL_PORT	18
#define	NR_LED		93
//#define	NR_LED		3
#define	NEOPIXEL_WS2812
#include "esp_log.h"
#include "esp_console.h"

#define STORAGE_NAMESPACE "PlasmaLamp"

static const char *TAG = "PlasmaLamp";

bool load_powerState();
void change_displayMode(short newMode,unsigned short reportFlags);
short load_displayMode();
void save_displayMode();

#define GPIO_LED GPIO_NUM_2

//#define	NEOPIXEL_SK6812
#define	NEOPIXEL_RMT_CHANNEL		RMT_CHANNEL_2

#ifndef M_PI
#define M_PI acos(-1.0)
#endif
float hypercos(float i){
        return  (cos(i)+1)/2;
}
inline float softring(float pos) {
  return (cos(
      (float)pos
  )/2)+0.5f;
}


unsigned int globalDelay=10000;
/* Slot is the CURRENT things running.
 MODE is the desired SETTING. These often equate - but a 
 MODE of 10 means we want to auto-advance SLOTs! */
int mode =0;
#define MODE_AUTOADVANCE (10)
bool powerState=false;
volatile short workphase=0;
unsigned short sparkle=128;
unsigned char flicker_r=0;
unsigned char flicker_g=0;
unsigned char flicker_b=0;
unsigned short numflicker=0;
short preset_running=-1;
unsigned short fade_in=512;
unsigned short fade_out=0;
time_t next_time=0;

int divround(const int n, const int d)
{
  return ((n < 0) ^ (d < 0)) ? ((n - d/2)/d) : ((n + d/2)/d);
}

/* TODO - Race condition changing fades wile plasma thread running??? */
/* TODO - Need MQTT report when done*/ 

/* Avoid unnecessary loops - i.e. if "X" told you to power on - Don't save back to "X" */
void plasma_powerOn(unsigned short reportFlags) {
  if (powerState) return;
  powerState = true;
  if (fade_out) {
    fade_in = fade_out;
    fade_out = 0;
  }
  else
    fade_in = 512;

  if (!(reportFlags & REPORTFLAG_NOMQTT))
    mqtt_report_powerState(true,reportFlags);

  
  if (!(reportFlags & REPORTFLAG_NONVS))
    save_powerState();
  workphase=0;

}

void plasma_powerOff(unsigned short reportFlags) {
  if (!powerState) return;
  powerState = false;
  if (fade_in) {
    fade_out = fade_in;
    fade_in=0;
  }
  else
    fade_out=512; // Start fade

  if (!(reportFlags & REPORTFLAG_NOMQTT))
    mqtt_report_powerState(false,reportFlags);

  if (!(reportFlags & REPORTFLAG_NONVS))
    save_powerState();
}
int load_preset(int slot);
static void IRAM_ATTR gpio_isr_handler(void* arg) {
	//printf("INT should go to xQueue\r\n");

  if (++mode==4)
    mode=0;
}
void hue_to_rgb(int hue, unsigned char *ro, unsigned char *go, unsigned char *bo) {
        float r,g,b;
        float max;
        float third = (2*M_PI)/3.0f;
	float h = ((float)hue)*2*M_PI/255.0f;
        r = hypercos(h);
        g = hypercos(h+third);
        b = hypercos(h+third+third);
        max = r;
        if (g>max) max=g;
        if (b>max) max=b;
        *ro = 255*(r/max);
        *go = 255*(g/max);
        *bo = 255*(b/max);
}

#define RINGS 6
short ringsize[RINGS]= {
  (4*8),(4*6),16,12,8,1
};

typedef struct ring_s {
  unsigned short start;
  unsigned short end;
  unsigned short size;
  double seq;
  unsigned short pos;
  unsigned short mode;
  double speed;
  unsigned short angle;
  unsigned short width;
  unsigned short len;
	unsigned char red;
	unsigned char green;
	unsigned char blue;
	unsigned long huespeed;
	unsigned long hue;			// We use top byte only
} ring_t;

ring_t  rings[RINGS];


typedef struct preset_s {
  ring_t rings[RINGS];
  unsigned short sparkle;
  unsigned short numflicker;
  unsigned char flicker_r;
  unsigned char flicker_g;
  unsigned char flicker_b;
} preset_t;

#define SEQSIZE 100
int getPixel(int p,int pos,int width, int len,int pixels,int angles) {
        float r;
      int a;
    int result=0;
    short ph; // DEBUG ONLY

    float scaledpos = ((float)p*(float)SEQSIZE) / (float) pixels;

    for (a=0;a<angles;a++) {

      float v =0;
        /* Pixel to Sequence */
      float seq = scaledpos;
        seq -= pos;

        /* Compensate for angle */
        seq += ((float) a*(float) SEQSIZE)/(float) angles;

        while (seq < (-SEQSIZE/2))
          seq += SEQSIZE;
        while (seq > (SEQSIZE/2))
          seq -= SEQSIZE;


        /* Normalize sequence number??? */

        /* Offset for current position */
        /* Scale to width Radians */
        if ((seq) > (width+len)) {
                r=M_PI;
                v=0;
                ph=0;
        }
        else if (seq < (-width-len)) {
                r=-M_PI;
                v=0;
                ph=5;
        }
        else if ((-len < seq) && (seq <= len))  {
                r = 0;
                v=1;
                ph=2;
        } else if ((seq) > (len)) {
                r = M_PI*(seq-len) / (float) width;
                v = softring(r);
                ph=1;
        }
        else if ((seq) > (-len-width)) {
                r = M_PI*(-len-seq) / (float) width;
                v = softring(r);
                ph=4;
        }
        else {
                r=-M_PI;
                v=0;
                ph=5;
        }
      //printf("%d %f %f %f %d\n",p,seq,r,v,ph);
      result += 255*v;
  }
  return (result > 255)? 255: result;
}

void advance_slot(){
  unsigned slot;
  if (mode == MODE_AUTOADVANCE)
    slot = preset_running+1;
  else 
    slot = mode;
  ESP_LOGI(TAG,"Advance to preset %d",slot);
  if ((load_preset(slot) == -1) && (slot != 0)) {
    ESP_LOGI(TAG,"Failed - trying preset 0");
    load_preset(0);
  }
  time(&next_time);
  next_time+=60;
}

void change_displayMode(short newMode,unsigned short reportFlags) {
  if (mode == newMode) return;
  mode = newMode;

  ESP_LOGI(TAG,"Change to mode %d",mode);


  fade_out = 512;
  time(&next_time);
  next_time+=60;
  if (!(reportFlags & REPORTFLAG_NOMQTT)) {
    mqtt_report_displayMode(newMode,reportFlags);
  }
  if (!(reportFlags & REPORTFLAG_NONVS)) {
    save_displayMode();
  }
}

static	void test_neopixel(void *parameters)
{
	pixel_settings_t px;
	uint32_t		pixels[NR_LED];
	int		i,r;
	int		rc;
  ring_t *rng;

	rc = neopixel_init(NEOPIXEL_PORT, NEOPIXEL_RMT_CHANNEL);
	ESP_LOGE("main", "neopixel_init rc = %d", rc);
	usleep(1000*1000);

	for	( i = 0 ; i < NR_LED ; i ++ )	{
		pixels[i] = 0;
	}

  /* Initialize ring info */
  rc=0;
  for (i=0;i<RINGS;i++) {
    memset(&rings[i],0,sizeof(ring_t));
    rings[i].start=rc;
    rings[i].size=ringsize[i];
    rings[i].end=rc+ringsize[i]-1;
    rings[i].width=5;
    rings[i].len=5;
    rings[i].angle=1;
    rc += ringsize[i];
    hue_to_rgb((i*255)/RINGS, &rings[i].red, &rings[i].green, &rings[i].blue);
  }

  rings[0].speed=1;
  rings[1].speed=1;
  rings[2].speed=1;
  rings[3].speed=1;
  rings[4].speed=1;
  rings[5].speed=1;
	px.pixels = (uint8_t *)pixels;
	px.pixel_count = NR_LED;
#ifdef	NEOPIXEL_WS2812
	strcpy(px.color_order, "GRB");
#else
	strcpy(px.color_order, "GRBW");
#endif

	memset(&px.timings, 0, sizeof(px.timings));
	px.timings.mark.level0 = 1;
	px.timings.space.level0 = 1;
	px.timings.mark.duration0 = 12;
#ifdef	NEOPIXEL_WS2812
	px.nbits = 24;
	px.timings.mark.duration1 = 14;
	px.timings.space.duration0 = 7;
	px.timings.space.duration1 = 16;
	px.timings.reset.duration0 = 600;
	px.timings.reset.duration1 = 600;
#endif
#ifdef	NEOPIXEL_SK6812
	px.nbits = 32;
	px.timings.mark.duration1 = 12;
	px.timings.space.duration0 = 6;
	px.timings.space.duration1 = 18;
	px.timings.reset.duration0 = 900;
	px.timings.reset.duration1 = 900;
#endif
	px.brightness = 0x80;
	np_show(&px, NEOPIXEL_RMT_CHANNEL);

  // If we have a preset - use it
  powerState = load_powerState();
  mode = load_displayMode();
  printf("Power state loaded from NVS is %s Mode %d\n",powerState?"ON":"OFF",mode);
  load_preset(mode == MODE_AUTOADVANCE ? 0 : mode); /* Mode and Slot are kind of equalish?? */
#if 0
  short pos=0;
  short color=0;
  short posrate=1;
  long seq=0;
  unsigned int hue=0;

  while(1) {
    usleep(2000*10);
    if (mode == 0) {
      hue_to_rgb(((hue++)>>4) & 0xff,&ro,&go,&bo);
      for	( int j = 0 ; j < NR_LED ; j ++ )	{
        if (j == pos) {
          np_set_pixel_rgbw(&px, j , ro, go, bo, 0);
        }
        else
          np_set_pixel_rgbw(&px, j , 0, 0, 0, 0);
      }
      np_show(&px, NEOPIXEL_RMT_CHANNEL);
    }
    if (pos++ == NR_LED)
      pos=0;
  }
  while(1) {
    /* Clear All */
    for	( int j = 0 ; j < NR_LED ; j ++ )	
          np_set_pixel_rgbw(&px, j , 0, 0, 0, 0);

    /* Handle each ring separately! */
    np_show(&px, NEOPIXEL_RMT_CHANNEL);
    usleep(2000*10);
  }
#endif
/* Start autoadvance */
  time(&next_time);
  next_time+=60;
  while(1) 
    if (powerState || fade_out){ 
    /* Clear All */
    int level=255;
    workphase=0;
    for	( int j = 0 ; j < NR_LED ; j ++ )	
          np_set_pixel_rgbw(&px, j , 0, 0, 0, 0);

    workphase++;

    if (!fade_out && mode == MODE_AUTOADVANCE && next_time) { 
      time_t current_time;
      time(&current_time);
      if (current_time >= next_time) {
        fade_out=512;
      }
    }

    if (fade_out >0) {
      fade_out--;
      level = fade_out/2;
    }

    if (fade_in > 0) {
      fade_in--;
      level = 255-(fade_in/2);
    }

    for (r=0;r<RINGS;r++) {
      rng = &rings[r];

    if (rng->mode == 0) {
      int p;
      for(p=0;p<rng->size;p++) {
        int v;
        v = getPixel(p,rng->seq,rng->width,rng->len,rng->size,rng->angle);
        np_set_pixel_rgbw_level(&px, rng->start + p , (v*rng->red)>>8,(v*rng->green)>>8,(v*rng->blue)>>8,0,level);
        
      }
	  }
    /* Flame Flicker */
    if (rng->mode == 1) {
      int p;
      for(p=0;p<rng->size;p++) {
        short r = 0xff;
        short g = (esp_random() & 0xff);
        short scale = (esp_random() & 0x3);
        r = r>>scale;
        g = r>>scale;
        np_set_pixel_rgbw_level(&px, rng->start + p , r,g,0,0,level);
        
      }
    }

      rng->seq+=rng->speed;
    while (rng->seq > SEQSIZE)
      rng->seq -= SEQSIZE;
    while (rng->seq < -SEQSIZE)
      rng->seq += SEQSIZE;
    if (rng->huespeed) {
      rng->hue += rng->huespeed;
      hue_to_rgb(rng->hue >> 24,&rng->red,&rng->green,&rng->blue);
    }
  }
    workphase++;
    if (sparkle) {
      int j = esp_random() %NR_LED;
      np_set_pixel_rgbw_level(&px, j , sparkle, sparkle, sparkle, 0,level);
    }
    workphase++;
    if (numflicker) {
      for (i=0;i<numflicker;i++){
            int j = esp_random() %NR_LED;
            np_set_pixel_rgbw_level(&px, j , flicker_r,flicker_g,flicker_b,0,level);
      }
    }
    /* Handle each ring separately! */
    //taskENTER_CRITICAL();
    //vTaskSuspendAll();
    workphase++;
    np_show(&px, NEOPIXEL_RMT_CHANNEL);
    workphase++;
    //xTaskResumeAll();
    //taskEXIT_CRITICAL();
    usleep(globalDelay);
    workphase++;

    // Do we need to advacne?
    if (fade_out == 1) {

      ESP_LOGI(TAG,"Advance Time");
      advance_slot();
    
      fade_out--;
      if (powerState)
        fade_in=512;
      }
    
  }
  else 
    usleep(globalDelay); // Power "off"
}

static int do_debug_cmd(int argc, char **argv) {
    printf( "Task Name\tStatus\tPrio\tHWM\tTask\tAffinity\n");
    char *stats_buffer = malloc(1024);
    vTaskList(stats_buffer);
    printf("%s\n", stats_buffer);
    free (stats_buffer);
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
    printf("Workphase %d\n",workphase);
    return(0);
}
static int do_cli_power(int argc, char **argv) {
  if (argc < 2)
    printf("Power state is %s\n",powerState?"on":"off");
  else if (!strcmp("on",argv[1])) {
    printf("Turning powerState ON\n");
    plasma_powerOn(REPORTFLAG_DESIRED);
  }
  else if (!strcmp("off",argv[1])) {
    printf("Turning powerState OFF\n");
    plasma_powerOff(REPORTFLAG_DESIRED);
  }
  else printf("Powerstate must be \"on\" or \"off\"\n");
  return 0;
}
static int do_cli_mode(int argc, char **argv) {
  short newmode=-1;
  printf("Mode  is %d\n",mode);
  if (argc < 2)
    return (0);
  else if (!strcmp("auto",argv[1])) {
    newmode = MODE_AUTOADVANCE;
  }
  else 
    newmode = atoi(argv[1]);
  
  printf("Changing mode to %d\n",newmode);
  change_displayMode(newmode,REPORTFLAG_DESIRED);
  return 0;
}
static int do_set_cmd(int argc, char **argv) {
  if (argc < 2)
    return 0;

  if (!strcmp("sparkle",argv[1]))
    sparkle = strtoul(argv[2],0L,0);
  if (!strcmp("flicker",argv[1])) {
	unsigned long rgb;
    rgb = strtoul(argv[2],0L,0);
	flicker_r=rgb>>16;
	flicker_g=(rgb>>8)&0xff;
	flicker_b=(rgb)&0xff;
}
  if (!strcmp("numflicker",argv[1]))
    numflicker = strtoul(argv[2],0L,0);

  if (!strcmp("delay",argv[1]))
    globalDelay = strtoul(argv[2],0L,0);

  printf("Sparkle is %d (0x%x)\n",sparkle,sparkle);
  printf("Delay is %d (0x%x)\n",globalDelay,globalDelay);
  printf("flicker is %x %x %x\n",flicker_r,flicker_g,flicker_b);
  printf("numflicker is %d (0x%x)\n",numflicker,numflicker);
  
  return 0;
}

static int do_dump_cmd(int argc, char **argv) {
  int i;
  ring_t *rng;

  printf("sparkle=%d;\n",sparkle);
  printf("globalDelay=%d;\n",globalDelay);
  printf("flicker_r=%d;\n",flicker_r);
  printf("flicker_g=%d;\n",flicker_g);
  printf("flicker_b=%d;\n",flicker_b);
  printf("numflicker=%d;\n",numflicker);

  for (i=0;i < RINGS;i++)
  {
    rng = &rings[i];
    printf(" rng[%d]->speed=%f; rng[%d]->pos=%d; rng[%d]->mode=%d; rng[%d]->seq=%f;\n",i,rng->speed,i,rng->pos,i,rng->mode,i,rng->seq);
    printf(" rng[%d]->red=%d; rng[%d]->green=%d; rng[%d]->blue=%d; rng[%d]->angle=%d;\n",i,rng->red,i,rng->green,i,rng->blue,i,rng->angle);
    printf(" rng[%d]->width=%d; rng[%d]->len=%d; rng[%d]->huespeed=%lu\n",i,rng->width,i,rng->len,i,rng->huespeed);
  } 
  return(0);
}

static int do_showring_cmd(int argc, char **argv) {
  int r;
  char *str1, *token, *subtoken, *subtoken2;
  char *saveptr1, *saveptr2;
  int j,i;

  printf("SHOWRINGS %d args\n",argc);
  ring_t *rng;
  for (i=0;i<argc;i++)
    printf("  %d - %s\n",i,argv[i]);
  if (argc < 2)
    return 1;

   for (j = 1, str1 = argv[1]; ; j++, str1 = NULL) {
       token = strtok_r(str1, ",", &saveptr1);
       if (token == NULL)
           break;

        subtoken = strtok_r(token, "-", &saveptr2);
        if (subtoken) {
                subtoken2 = strtok_r(NULL, "-", &saveptr2);
                if (!subtoken2) subtoken2=subtoken;
                for (r=strtoul(subtoken,0L,0);r<=strtoul(subtoken2,0L,0) && r < RINGS;r++)
								{
												rng = &rings[r];
												printf("Ring %d\n",r);
												printf(" Was Speed %f Pos %d Mode %d seq %f \n  color %2.2x:%2.2x:%2.2x Ang %d Width %d Len %d huespeed %lu\n",
													rng->speed,rng->pos,rng->mode,rng->seq,rng->red,rng->green,rng->blue,
													rng->angle,rng->width,rng->len,rng->huespeed);
												for (i=2;i<argc;i+=2) {
													if (!strcmp("speed",argv[i]))
														rng->speed=strtof(argv[i+1],0L);  
													else if (!strcmp("mode",argv[i]))
														rng->mode=strtoul(argv[i+1],0L,0);  
													else if (!strcmp("pos",argv[i]))
														rng->pos=strtoul(argv[i+1],0L,0);  
													else if (!strcmp("seq",argv[i]))
														rng->seq=strtof(argv[i+1],0L);  
													else if (!strcmp("angle",argv[i]))
														rng->angle=strtoul(argv[i+1],0L,0);  
													else if (!strcmp("width",argv[i]))
														rng->width=strtoul(argv[i+1],0L,0);  
													else if (!strcmp("huespeed",argv[i]))
														rng->huespeed=strtoul(argv[i+1],0L,0);  
													else if (!strcmp("len",argv[i]))
														rng->len=strtoul(argv[i+1],0L,0);  
													else if (!strcmp("rgb",argv[i])) {
														unsigned long cc = strtoul(argv[i+1],0L,16);
														rng->red = (cc >> 16);
														rng->green = (cc & 0x00ff00) >> 8;
														rng->blue = (cc & 0x0000ff);
													}
													else if (!strcmp("hue",argv[i])) {
														hue_to_rgb(strtoul(argv[i+1],0L,0), &rng->red, &rng->green, &rng->blue);
													}
												printf(" Now Speed %f Pos %d Mode %d seq %f \n  color %2.2x:%2.2x:%2.2x Ang %d Width %d Len %d huespeed %lu\n",
													rng->speed,rng->pos,rng->mode,rng->seq,rng->red,rng->green,rng->blue,
													rng->angle,rng->width,rng->len,rng->huespeed);
												}
								}
       }
   }




  return 0;
}

static void initialize_console(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "LavaLamp>";
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    const esp_console_cmd_t ring_cmd = {
        .command = "ring",
        .help = "SHow Ring Parameters",
        .hint = NULL,
        .func = &do_showring_cmd,
        .argtable = 0L
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ring_cmd));
        const esp_console_cmd_t dump_cmd = {
        .command = "dump",
        .help = "Dump state",
        .hint = NULL,
        .func = &do_dump_cmd,
        .argtable = 0L
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&dump_cmd));
    const esp_console_cmd_t set_cmd = {
        .command = "set",
        .help = "Set Parameters",
        .hint = NULL,
        .func = &do_set_cmd,
        .argtable = 0L
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_cmd));
    const esp_console_cmd_t debug_cmd = {
        .command = "debug",
        .help = "debug info",
        .hint = NULL,
        .func = &do_debug_cmd,
        .argtable = 0L
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&debug_cmd));
    const esp_console_cmd_t power_cmd = {
        .command = "power",
        .help = "Power State",
        .hint = NULL,
        .func = &do_cli_power,
        .argtable = 0L
    };    
    
    ESP_ERROR_CHECK(esp_console_cmd_register(&power_cmd));
    
    const esp_console_cmd_t mode_cmd = {
        .command = "mode",
        .help = "Display Mode",
        .hint = NULL,
        .func = &do_cli_mode,
        .argtable = 0L
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mode_cmd));
    ESP_LOGI(TAG,"Console handlers initialized\n");
}

static	void console(void *parameters) {
    /* Register commands */
    initialize_console();
    printf("Console init done\n");
    while(1) {
      vTaskDelay(1000*1000);
    }
}

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
        } else if (find_regress("Power",buf)) {
            char *s=find_regress("sparkle",buf);
            if (s && !strcmp(s,"on"))
              plasma_powerOn(0);
            else 
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

static httpd_handle_t start_webserver(void)
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

static void disconnect_handler(void* arg, esp_event_base_t event_base, 
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base, 
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
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
  xTaskCreatePinnedToCore(&console, "Console", 8192, NULL, 1, NULL,0);
}

