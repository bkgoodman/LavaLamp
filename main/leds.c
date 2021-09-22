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

#include "data.h"

#define	NEOPIXEL_WS2812
static const char *TAG = "Neopixels";
#define	NEOPIXEL_RMT_CHANNEL		RMT_CHANNEL_2
#define	NEOPIXEL_PORT	18
#define	NR_LED		93

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

short ringsize[RINGS]= {
  (4*8),(4*6),16,12,8,1
};

ring_t  rings[RINGS];

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

void test_neopixel(void *parameters)
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

