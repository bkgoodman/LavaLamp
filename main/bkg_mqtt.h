void mqtt_app_start(void);
void plasma_powerOn(unsigned short reportFlags);
void plasma_powerOff(unsigned short reportFlags);
void save_powerState();
void mqtt_report_powerState(bool powerState);

// Power Stage Change - do not report flags
#define REPORTFLAG_NOMQTT 1
#define REPORTFLAG_NONVS 2