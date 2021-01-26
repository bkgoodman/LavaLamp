#include 	<stdio.h>
#include	<string.h>
#include	"esp_event.h"	//	for usleep

#include	"neopixel.h"
#include	<esp_log.h>
#include <math.h>

#define GPIO_INPUT_IO_0    0
#define ESP_INTR_FLAG_DEFAULT 0
#define	NEOPIXEL_PORT	18
#define	NR_LED		93
//#define	NR_LED		3
#define	NEOPIXEL_WS2812
//#define	NEOPIXEL_SK6812
#define	NEOPIXEL_RMT_CHANNEL		RMT_CHANNEL_2
float hypercos(float i){
        return  (cos(i)+1)/2;
}

int mode =0;
int divround(const int n, const int d)
{
  return ((n < 0) ^ (d < 0)) ? ((n - d/2)/d) : ((n + d/2)/d);
}

static void IRAM_ATTR gpio_isr_handler(void* arg) {
	//printf("INT should go to xQueue\r\n");

  if (++mode==4)
    mode=0;
}
void hue_to_rgb(int hue, unsigned char *ro, unsigned char *go, unsigned char *bo) {
        float r,g,b;
        float max;
        float pi = 3.14159;
        float third = ((float) 2.0f)*pi/3.0f;
        r = hypercos(hue);
        g = hypercos(hue+third);
        b = hypercos(hue+third+third);
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

typedef struct ring {
  unsigned short start;
  unsigned short end;
  unsigned short size;
  unsigned long seq;
  unsigned short pos;
  unsigned short speed;
} ring;

ring  rings[RINGS];

static	void
test_neopixel()
{
	pixel_settings_t px;
  unsigned char ro,go,bo;
	uint32_t		pixels[NR_LED];
	int		i,r;
	int		rc;
  ring *rng;

	rc = neopixel_init(NEOPIXEL_PORT, NEOPIXEL_RMT_CHANNEL);
	ESP_LOGE("main", "neopixel_init rc = %d", rc);
	usleep(1000*1000);

	for	( i = 0 ; i < NR_LED ; i ++ )	{
		pixels[i] = 0;
	}

  /* Initialize ring info */
  rc=0;
  for (i=0;i<RINGS;i++) {
    memset(&rings[i],0,sizeof(ring));
    rings[i].start=rc;
    rings[i].size=ringsize[i];
    rings[i].end=rc+ringsize[i]-1;
    rc += ringsize[i];
  }

  rings[0].speed=60;
  rings[1].speed=55;
  rings[2].speed=70;
  rings[3].speed=63;
  rings[4].speed=54;
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
  while(1) {
    /* Clear All */
    for	( int j = 0 ; j < NR_LED ; j ++ )	
          np_set_pixel_rgbw(&px, j , 0, 0, 0, 0);

    for (r=0;r<RINGS;r++) {
      int p;

      rng = &rings[r];
      //p = (rng->pos % 60) * rng->size / 60;
      p = divround((rng->seq % rng->speed) * rng->size , rng->speed);
      hue_to_rgb(r*256/RINGS, &ro, &go, &bo);
      np_set_pixel_rgbw(&px, rng->start + p , ro, go, bo, 0);
      rng->seq++;
    }

    int j = esp_random() %NR_LED;
    np_set_pixel_rgbw(&px, j , 255, 255, 255, 0);
    /* Handle each ring separately! */
    np_show(&px, NEOPIXEL_RMT_CHANNEL);
    usleep(2000*10);
  }
}

extern	void
app_main (void)
{
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
	test_neopixel();
}

