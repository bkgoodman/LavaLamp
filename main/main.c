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
#include "esp_log.h"
#include "esp_console.h"

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


int mode =0;
unsigned short sparkle=128;
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

typedef struct ring_s {
  unsigned short start;
  unsigned short end;
  unsigned short size;
  unsigned long seq;
  unsigned short pos;
  unsigned short mode;
  unsigned short speed;
  unsigned short angle;
  unsigned short width;
  unsigned short len;
	unsigned char red;
	unsigned char green;
	unsigned char blue;
} ring_t;

ring_t  rings[RINGS];

#define SEQSIZE 100
int getPixel(int p,int pos,int width, int len,int pixels,int angles) {
        float r;
      int a;
    int result=0;


    for (a=0;a<angles;a++) {

      float v =0;
        /* Pixel to Sequence */
      float seq = ((float)p*(float)SEQSIZE) / (float) pixels;
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
                //ph=0;
        }
        else if ((seq) > (len)) {
                r = M_PI*(seq-len) / (float) width;
                v = softring(r);
                //ph=1;
        }
        else if ((seq) > 0) {
                r = 0;
                v=1;
                //ph=2;
        }
        else if ((seq) > (-len)) {
                r = 0;
                v=1;
                //ph=3;
        }
        else if ((seq) > (-len-width)) {
                r = M_PI*(-len-seq) / (float) width;
                v = softring(r);
                //ph=4;
        }
        else {
                r=-M_PI;
                v=0;
                //ph=5;
        }
      result += 255*v;
  }
  return (result > 255)? 255: result;
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
    hue_to_rgb(i*256/RINGS, &rings[i].red, &rings[i].green, &rings[i].blue);
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
      int p,a;

      rng = &rings[r];

	if (rng->mode == 0) {
			for (a=0;a<=rng->angle;a++) {
				int seq = rng->seq;
				p = divround((seq % rng->speed) * rng->size , rng->speed);
				p += divround(rng->size*a,rng->angle+1);
				p = p % rng->size;
				np_set_pixel_rgbw(&px, rng->start + p , rng->red,rng->green,rng->blue,0);
			}
	}
	if (rng->mode == 1) {
		int p;
		for(p=0;p<rng->size;p++) {
			int v;
			v = getPixel(p,rng->seq,rng->width,rng->len,rng->size,rng->angle);
			np_set_pixel_rgbw(&px, rng->start + p , (v*rng->red)>>8,(v*rng->green)>>8,(v*rng->blue)>>8,0);
			
		}
	}

#if 0
			for (a=0;a<rng->size;a++) {
				float scale = hypercos(
				np_set_pixel_rgbw(&px, rng->start + p , rng->red*scale,rng->green*scale,rng->blue*scale,0);
			}
#endif
      rng->seq++;
    }

    if (sparkle) {
      int j = esp_random() %NR_LED;
      np_set_pixel_rgbw(&px, j , sparkle, sparkle, sparkle, 0);
    }
    /* Handle each ring separately! */
    np_show(&px, NEOPIXEL_RMT_CHANNEL);
    usleep(1000*10);
  }
}

static int do_set_cmd(int argc, char **argv) {
  if (argc < 2)
    return 0;
  if (!strcmp("sparkle",argv[1]))
    printf("Sparkle was %d (0x%x)\n",sparkle,sparkle);

  if (argc < 3)
    return 0;

  if (!strcmp("sparkle",argv[1]))
    sparkle = strtoul(argv[2],0L,0);

  if (!strcmp("sparkle",argv[1]))
    printf("Sparkle is %d (0x%x)\n",sparkle,sparkle);
  
  return 0;
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
												printf(" Was Speed %d Pos %d Mode %d seq %lu \n  color %2.2x:%2.2x:%2.2x Ang %d Width %d Len %d\n",
													rng->speed,rng->pos,rng->mode,rng->seq,rng->red,rng->green,rng->blue,
													rng->angle,rng->width,rng->len);
												for (i=2;i<argc;i+=2) {
													if (!strcmp("speed",argv[i]))
														rng->speed=strtoul(argv[i+1],0L,0);  
													else if (!strcmp("mode",argv[i]))
														rng->mode=strtoul(argv[i+1],0L,0);  
													else if (!strcmp("pos",argv[i]))
														rng->pos=strtoul(argv[i+1],0L,0);  
													else if (!strcmp("seq",argv[i]))
														rng->seq=strtoul(argv[i+1],0L,0);  
													else if (!strcmp("angle",argv[i]))
														rng->angle=strtoul(argv[i+1],0L,0);  
													else if (!strcmp("width",argv[i]))
														rng->width=strtoul(argv[i+1],0L,0);  
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
												printf(" Now Speed %d Pos %d Mode %d seq %lu \n  color %2.2x:%2.2x:%2.2x Ang %d Width %d Len %d\n",
													rng->speed,rng->pos,rng->mode,rng->seq,rng->red,rng->green,rng->blue,
													rng->angle,rng->width,rng->len);
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
    const esp_console_cmd_t set_cmd = {
        .command = "set",
        .help = "Set Parameters",
        .hint = NULL,
        .func = &do_set_cmd,
        .argtable = 0L
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_cmd));
}

static	void console(void *parameters) {
    /* Register commands */
    initialize_console();
    printf("Console init done\n");
    while(1) {
      vTaskDelay(100*1000);
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
	//test_neopixel();
  xTaskCreate(&test_neopixel, "Neopixels", 8192, NULL, 5, NULL);
  xTaskCreate(&console, "Console", 8192, NULL, 5, NULL);
}

