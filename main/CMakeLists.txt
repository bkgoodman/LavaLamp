set(srcs 
		"main.c" 
    "plasma_mqtt.c"
    "telnet.c"
		"leds.c"
		"console.c"
		"http.c"
    )


idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "."
		EMBED_FILES "index.html"
                    )

