# ESP Light Control MQTT

ESP32 firmware for controlling a WS2812B LED strip over MQTT. The controller
supports static colors, four brightness levels, and a rainbow animation. Wi-Fi
and MQTT reconnect automatically without stopping LED updates.

## Hardware

- ESP32 development board
- WS2812B LED strip with 160 LEDs
- External 5 V power supply sized for the LED strip
- LED data connected to GPIO 13
- Common ground between the ESP32 and LED power supply

Do not power a 160-LED strip from the ESP32's 5 V pin. Use an external supply
and suitable power injection for the strip. A data-line resistor and a capacitor
across the strip's power input are also recommended.

## Software

- [PlatformIO](https://platformio.org/)
- Espressif ESP32 Arduino framework
- [FastLED](https://fastled.io/)
- [PubSubClient](https://pubsubclient.knolleary.net/)
- An MQTT broker reachable from the ESP32 network

Dependencies are installed automatically from `platformio.ini`.

## Configuration

1. Copy `include/secrets.example.h` to `include/secrets.h`.
2. Enter the Wi-Fi, MQTT, and OTA credentials in `include/secrets.h`.
3. Set `MQTT_SERVER`, `MQTT_PORT`, and `MQTT_TOPIC` near the top of
   `src/main.cpp`.
4. Adjust `DATA_PIN` and `NUM_LEDS` in `src/main.cpp` if the hardware differs.

`include/secrets.h` is excluded from Git. Do not commit credentials. The OTA
password in this file must match the `--auth` value in the local `ota.ini`.

## MQTT

The controller subscribes to:

```text
led/control
```

Commands are case-insensitive and surrounding whitespace is ignored.

| Command | Action |
| --- | --- |
| `ON` | Turn on the last selected static color |
| `OFF` | Turn off the strip |
| `NEXTCOLOR` | Select the next static color |
| `BRIGHTUP` | Increase brightness by one level |
| `BRIGHTDOWN` | Decrease brightness by one level |
| `PARTY` | Start the rainbow animation |
| `WHITE` | Select white |
| `SUN` | Select warm white |
| `ORANGE` | Select orange |
| `BLUE` | Select blue |
| `RED` | Select red |
| `GREEN` | Select green |

Example using Mosquitto clients:

```sh
mosquitto_pub -h 192.168.0.23 -u USERNAME -P PASSWORD \
  -t led/control -m PARTY
```

Each ESP32 derives a unique MQTT client ID from its hardware ID, allowing
multiple controllers to use the same broker without disconnecting one another.

## Build And Upload

From the project directory:

```sh
pio run
pio run --target upload
pio device monitor
```

The serial monitor runs at 115200 baud. In VS Code, the corresponding PlatformIO
Build, Upload, and Monitor commands can be used instead.

If upload cannot find a port, verify that the board's USB-to-serial driver is
installed and that a COM port appears in Device Manager. Some ESP32 boards
require holding the `BOOT` button while the upload begins.

## OTA Updates

The first OTA-enabled firmware must be uploaded over USB. After it connects to
Wi-Fi, future firmware can be uploaded from PlatformIO using the
`esp32dev_ota` environment:

```sh
pio run --environment esp32dev_ota --target upload
```

The local `ota.ini` targets `192.168.0.29` and contains the OTA authentication
argument. It is excluded from Git because the authentication value must match
`OTA_PASSWORD` in `include/secrets.h`. For another installation, copy
`ota.example.ini` to `ota.ini`, then set the device IP, computer LAN IP, and
matching password. The computer LAN IP is required so the ESP32 can connect
back to PlatformIO's temporary upload server.

In VS Code, select `esp32dev_ota` with the PlatformIO environment selector and
use the normal Upload command. Keep the `esp32dev` environment available for
the initial USB installation and recovery. The computer and ESP32 must be able
to reach each other over the network.

## Runtime Behavior

- The strip starts off at brightness level 128.
- Wi-Fi reconnection is attempted every 10 seconds.
- MQTT reconnection is attempted every 2 seconds while Wi-Fi is connected.
- Network operations use a 2-second timeout.
- Static color and brightness state are held in memory and reset after reboot.
