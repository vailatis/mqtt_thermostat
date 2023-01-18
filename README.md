# mqtt_thermostat
A complete standalone chrono-thermostat able to communicate via MQTT over WiFi

Questo progetto nasce dalla necessità di avere un termostato per casa non solo capace di poter accendere e spegnere la caldaia al raggiungimento di una  temperatura impostabile, ma anche di poter gestire le varie temperatune nelle varie fasce orarie, giorno per giorno.
In aggiunta quindi alla possibilità di eseguire una programmazione settimanale, questo termostato ha la poossibilità di definire un programma "Holiday" giornaliero da utilizzare in giornate particolari come ferie, festività o altro, dove si è a casa per esempio in settimana e la normale programmazione settimanale non prevede ad esempio la presenza in casa di nessuno.
In aggiunta questo termostato permette di essere configurato con la propria WiFi di casa tramite WPA, e di agganciarsi ad un server MQTT per poter pubblicare il completo stato del termostato e di poter esssere comandato via comandi MQTT da remoto, ad esempio per poter essere integrato su HomeAssistant, come ho fatto io.
