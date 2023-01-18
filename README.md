# Introduzione
A complete standalone chrono-thermostat able to communicate via MQTT over WiFi

Questo progetto nasce dalla necessità di avere un termostato per casa non solo capace di poter accendere e spegnere la caldaia al raggiungimento di una  temperatura impostabile, ma anche di poter gestire le varie temperatune nelle varie fasce orarie, giorno per giorno.
In aggiunta quindi alla possibilità di eseguire una programmazione settimanale, questo termostato ha la poossibilità di definire un programma "Holiday" giornaliero da utilizzare in giornate particolari come ferie, festività o altro, dove si è a casa per esempio in settimana e la normale programmazione settimanale non prevede ad esempio la presenza in casa di nessuno.
In aggiunta questo termostato permette di essere configurato con la propria WiFi di casa tramite WPA, e di agganciarsi ad un server MQTT per poter pubblicare il completo stato del termostato e di poter esssere comandato via comandi MQTT da remoto, ad esempio per poter essere integrato su HomeAssistant, come ho fatto io.

![Home Screen](./sample_pictures/1_Home.jpg)

Il termostato è basato su un ESP32 inserito in un kit venduto dalla AZ-Delivery che comprende:
- un contenitore predisposto per il fissagigo a muro o su scatole 503
- un circuito stampato in cui è presente uno stadio di alimentazione con ingresso 9..24V, un cicalino pilotabile, la connessione al display ed un'area di prototipazione millefori
- un display da 2.2" o da 2.8" (si può scegliere tra una di queste due dimensioni per il display), il mio progetto è basato sul display da 2.8" anche per una questione di dimensioni del contenitore e di quello che ci va aggiunto per utilizzare la soluzione come termostato

Il materiale aggiuntivo è un relè miniatura da 5V con contatti N.O.
Un transistor BC.537 o un NPN equivalente
Una resistenza da 2.2 Kohm per connettere la base del transistor ad un GPIO dell'ESP32
Un real-time clock su una break-board DS.3231 con protocollo I2C
Un sensore di temperatura/umindità Si7021 su break-board con protocollo I2C

Il display presente nel kit è un ILI.9341 con touch resistivo, con una risoluzione di 320x200 ed interfaccia SPI e viene pilotato tramite la libreria Arduino TFT_eSPI, una tra le più veloci e performanti per micro-controllori ESP.

# Schema Elettrico
Qui sotto uno schema elettrico dei collegamenti dei componenti aggiuntivi da me aggiunti, in quanto lo schema dei collegamenti del display e del cicalino è presente nella documentazione PDF del kit AZ-Delivery.
![Home Screen](./sample_pictures/Schematics.jpg)


# Materiale e riferimenti
Qui sotto alcuni links al materiale che ho utilizzato. Non sono per nulla affiliato ai fornitori di questi materiali, li ho inseriti solamente come riferimento per gli oggetti che ho personalmente utilizzato e per tutti quelli che non hanno voglia di cercare i componenti:

- [Kit Contenitore+Display](https://amzn.eu/d/6OSxUhz)
- [BreakBoard DS.3231](https://amzn.eu/d/7noOq9g)
- [BreakBoard Si.7021](https://amzn.eu/d/eylJAoo)
