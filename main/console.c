#include 	<stdio.h>
#include	<string.h>
#include	"esp_event.h"	//	for usleep
#include "esp_log.h"
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_console.h"

#include "lwip/sockets.h"
#include "data.h"

static const char *TAG = "Console";
#include <stdarg.h>

#define TXBUFSIZE (100)
#define NOSOCKET ((void *) 0xFFFFFFFF)

typedef struct telnet_commands_s {
  const char *cmd;
  int (*func)(void *,int , char **); 
} telnet_commands_t;

static void printf_socket(void *context, const char *format, va_list args) {
  char txbuf[TXBUFSIZE+1];
  int sock = (int) context;
  int len = vsnprintf(txbuf,TXBUFSIZE,format,args);

  int to_write = len;
            while (to_write > 0) {
                int written = send(sock, txbuf + (len - to_write), to_write, 0);
                if (written < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                }
                to_write -= written;
            }
}

static void printf_console(void *context, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    if (context==NOSOCKET)
      vprintf(format, args);
    else
      printf_socket(context,format, args);

    va_end(args);
}

static int generic_debug_cmd(void *context,int argc, char **argv) {
#if 1
    printf_console(context,  "Task Name\tStatus\tPrio\tHWM\tTask\tAffinity\n");
    char *stats_buffer = malloc(1024);
    vTaskList(stats_buffer);
    printf_console(context, "%s\n", stats_buffer);
    free (stats_buffer);
#endif
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
    printf_console(context, "Workphase %d\n",workphase);
    return(0);
}

static int do_debug_cmd(int argc, char **argv) {
  return generic_debug_cmd(NOSOCKET,argc,argv);
}

static int generic_cli_power(void *context, int argc, char **argv) {
  if (argc < 2)
    printf_console(context, "Power state is %s\n",powerState?"on":"off");
  else if (!strcmp("on",argv[1])) {
    printf_console(context, "Turning powerState ON\n");
    plasma_powerOn(REPORTFLAG_DESIRED);
  }
  else if (!strcmp("off",argv[1])) {
    printf_console(context, "Turning powerState OFF\n");
    plasma_powerOff(REPORTFLAG_DESIRED);
  }
  else printf_console(context, "Powerstate must be \"on\" or \"off\"\n");
  return 0;
}

static int do_cli_power(int argc, char **argv) {
  return generic_cli_power(NOSOCKET,argc,argv);
}

static int generic_cli_mode(void *context, int argc, char **argv) {
  short newmode=-1;
  printf_console(context, "Mode  is %d\n",mode);
  if (argc < 2)
    return (0);
  else if (!strcmp("auto",argv[1])) {
    newmode = MODE_AUTOADVANCE;
  }
  else 
    newmode = atoi(argv[1]);
  
  printf_console(context, "Changing mode to %d\n",newmode);
  change_displayMode(newmode,REPORTFLAG_DESIRED);
  return 0;
}

static int do_cli_mode(int argc, char **argv) {
  return generic_cli_mode(NOSOCKET,argc,argv);
}

static int generic_set_cmd(void *context,int argc, char **argv) {
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

  printf_console(context, "Sparkle is %d (0x%x)\n",sparkle,sparkle);
  printf_console(context, "Delay is %d (0x%x)\n",globalDelay,globalDelay);
  printf_console(context, "flicker is %x %x %x\n",flicker_r,flicker_g,flicker_b);
  printf_console(context, "numflicker is %d (0x%x)\n",numflicker,numflicker);
  
  return 0;
}

static int do_set_cmd(int argc, char **argv) {
  return generic_set_cmd(NOSOCKET,argc,argv);
}

static int generic_dump_cmd(void *context, int argc, char **argv) {
  int i;
  ring_t *rng;

  printf_console(context, "sparkle=%d;\n",sparkle);
  printf_console(context, "globalDelay=%d;\n",globalDelay);
  printf_console(context, "flicker_r=%d;\n",flicker_r);
  printf_console(context, "flicker_g=%d;\n",flicker_g);
  printf_console(context, "flicker_b=%d;\n",flicker_b);
  printf_console(context, "numflicker=%d;\n",numflicker);

  for (i=0;i < RINGS;i++)
  {
    rng = &rings[i];
    printf_console(context, " rng[%d]->speed=%f; rng[%d]->pos=%d; rng[%d]->mode=%d; rng[%d]->seq=%f;\n",i,rng->speed,i,rng->pos,i,rng->mode,i,rng->seq);
    printf_console(context, " rng[%d]->red=%d; rng[%d]->green=%d; rng[%d]->blue=%d; rng[%d]->angle=%d;\n",i,rng->red,i,rng->green,i,rng->blue,i,rng->angle);
    printf_console(context, " rng[%d]->width=%d; rng[%d]->len=%d; rng[%d]->huespeed=%lu\n",i,rng->width,i,rng->len,i,rng->huespeed);
  } 
  return(0);
}
static int do_dump_cmd(int argc, char **argv) {
  return generic_dump_cmd(NOSOCKET,argc,argv);
}

static int generic_showring_cmd(void *context, int argc, char **argv) {
  int r;
  char *str1, *token, *subtoken, *subtoken2;
  char *saveptr1, *saveptr2;
  int j,i;

  printf_console(context, "SHOWRINGS %d args\n",argc);
  ring_t *rng;
  for (i=0;i<argc;i++)
    printf_console(context, "  %d - %s\n",i,argv[i]);
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
												printf_console(context, "Ring %d\n",r);
												printf_console(context, " Was Speed %f Pos %d Mode %d seq %f \n  color %2.2x:%2.2x:%2.2x Ang %d Width %d Len %d huespeed %lu\n",
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
												printf_console(context, " Now Speed %f Pos %d Mode %d seq %f \n  color %2.2x:%2.2x:%2.2x Ang %d Width %d Len %d huespeed %lu\n",
													rng->speed,rng->pos,rng->mode,rng->seq,rng->red,rng->green,rng->blue,
													rng->angle,rng->width,rng->len,rng->huespeed);
												}
								}
       }
   }
  return 0;
}

static int do_showring_cmd(int argc, char **argv) {
  return generic_showring_cmd(NOSOCKET,argc,argv);
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

static int generic_help_cmd(void *context, int argc, char **argv);

telnet_commands_t telnet_commands[] = {
  {
    .cmd="debug",
    .func=&generic_debug_cmd
  },
  {
    .cmd="dump",
    .func=&generic_dump_cmd
  },
  {
    .cmd="showring",
    .func=&generic_showring_cmd
  },
  {
    .cmd="set",
    .func=&generic_set_cmd
  },
  {
    .cmd="mode",
    .func=&generic_cli_mode
  },
  {
    .cmd="power",
    .func=&generic_cli_power
  },
  {
    .cmd="help",
    .func=&generic_help_cmd
  },
  {
    .cmd=0L,
    .func=0L
  }
};
void printf_socket_static(void *context, const char *arg1, ...)
{
   va_list ap;
   va_start(ap, arg1);
   printf_socket(context,arg1, ap);
   va_end(ap);
}
static int generic_help_cmd(void *context, int argc, char **argv) {
  telnet_commands_t  *cmd;
  for (cmd=telnet_commands;cmd->cmd;cmd++) {
    printf_socket_static(context,"%s\n",cmd->cmd);
  }
  return (0);
}

#define MAX_ARGV (10)
void telnet_command_handler(int sock, char *buffer) {
  char *saveptr;
  char *str1,*token;
  int argc=0;
  telnet_commands_t  *cmd;
  char *argv[MAX_ARGV];
  for (argc = 0, str1 = buffer ; (argc<MAX_ARGV);  argc++, str1 = NULL) {
      token = strtok_r(str1, " ", &saveptr);
      if (token == NULL)
          break;
      argv[argc]=token;
  }

  for (cmd=telnet_commands;cmd->cmd;cmd++) {
    if (!strcmp(argv[0],cmd->cmd)) {
      cmd->func((void *)sock,argc,argv);
      break;
    }
  }
}

void console(void *parameters) {
    /* Register commands */
    initialize_console();
    printf("Console init done\n");
    while(1) {
      vTaskDelay(1000*1000);
    }
}

