# N-Break — Econet-over-WiFi (AUN) Bridge

## Overview

N-Break is an Econet-over-WiFi interface that allows a retro BBC Micro to
communicate with an AUN fileserver (or other device) over a modern IP network.

It recreates the classic Econet experience using inexpensive, readily
available hardware - no custom PCBs or extensive soldering required.

## Hardware Requirements

To build the interface, you will need:

- **1 × ESP32-C6 module**
  Example: <a href="https://it.aliexpress.com/item/1005007399157637.html" target="_blank" rel="noopener noreferrer">https://it.aliexpress.com/item/1005007399157637.html</a>
  Approx. price: €5.39

- **2 × RS422/RS485 transceiver modules**
  Example: <a href="https://it.aliexpress.com/item/32688467460.html" target="_blank" rel="noopener noreferrer">https://it.aliexpress.com/item/32688467460.html</a>
  Approx. price: €1.69 each

- **1 × 5-pin DIN plug** and twisted-pair cable (e.g., CAT5)

- **Jumper/Dupont wires** to connect everything

- **3 resistors** for Econet termination and biasing:
  - 1 × **110 Ω** termination
  - 2 × **550 Ω** idle bias

## Hardware Assembly

Connect the components as shown in the schematic:

![Hardware schematic](docs/Schematic.png)

**Note:** Power the RS485 driver modules from **3.3 V**.

Final assembly complete with Econet plug ready for use:

![Final assy](docs/hardware.jpg)

## Firmware Installation

Firmware is built and flashed using **idf.py** with the ESP-IDF toolchain.

1. Install ESP-IDF:
   <a href="https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html" target="_blank" rel="noopener noreferrer">https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html</a>

2. Build and flash the firmware:

   ```shell
   idf.py flash
   ```

## Device Configuration and monitoring

After flashing, the device will start a Wi-Fi access point named nbreak-econet.

1. Connect to the access point.

2. Open a browser and go to http://192.168.4.1. You can select the menu items on the left.

![Web interface stats page](docs/WebInterface.png)

3. After connecting WiFi and verifying you have access, you can configure the Econet/AUN settings.

The example screenshot below is for a BBC Micro whose station ID is 127 and a fileserver at 192.168.0.1.

![Web interface econet config](docs/EconetCfg.png)

The first table, the Econet Stations list, should specify the station numbers on your Econet that you intend to expose to the IP network. N-Break will monitor the designated port for traffic addressed to these stations. Currently you can have a maximum of 5. Additional entries
will not be loaded.

The second table defines the AUN IP hosts that you want to present to the Econet network. N-Break will listen on the Econet for these station IDs and respond on their behalf, forwarding the traffic to the specified IP address and port.

For communications to be successful, you need at least one entry in both tables. 

## AUN Fileserver Setup

You can use aund as your Econet fileserver:

    https://github.com/sai2791/aund/

Example configuration files are included in the contrib/ directory.

## Enjoy!
