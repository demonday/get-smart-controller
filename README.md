# Modern Replacement for "GET" Smart Wireless Lighting System Controller

## Project Intro
I have a aging Wireless Lighting System from a UK company called GET, from circa 2007. Its now so old in fact, that its no longer manufatured, and the company is no longer in existance.
The setup includes a battery-operated controller (replacing the traditional UK light switch) and some mains supplied receivers to operate the lights. I buried the recievers behind the plasterboard in my apartment's ceiling when I installed the system.
Its operated flawlessly since then, I don't think I've ever changed the batteries in the controller, which is testament to its solid design.

It is however, missing integration with home automation (I use Home Assistant & Alexa). Its now a deal breaker. I could replace them with somethine else, and repair the plaster.... 
... but rather than add to the e-waste pile, I thought I'd put one of those microcontroller's I have in a box to good use.

## Side Quests
Typically for me, I'm also diving into a few related technologies as part of this project:
- **Zephyr**: I'm curious to learn this framework.
- **Home Assistant Integration**: I use HA for all my other stuff so this is given.
- **Thread/Matter**: I have tons Zigbee devices already, and want learn more about the newer connected device protocols.

# Reverse Engineering the Controller
After some snooping inside the contoller it seems to be 433Mhz FSK, based on a HiMark TX4915-LF RF chip. Only datasheets HiMark TX4915 say its for ASK, but the silksreen on the transmiter clearly says 433 FSK.

## My Controller Setup
From the [GET Smart Wireless Lighting System Manual](https://www.tlc-direct.co.uk/Technical/DataSheets/GET/Smart_user.pdf) I'm using the controller in "Dimming Mode", with 2 lights, on buttons 1 and 2 respectively.

## Sniffing the data
Before I opened the controller 
Bring on the Hack RF One. 

# Upgrading the GET - Smart Wireless Lighting System Controller

## Reach Out
For collaboration or feedback, please feel free to contact me at [ngormley at armadillo.ie]

*Thank you for your interest*
