# LED Toggle BLE Application

A simple Zephyr application that allows toggling an LED over BLE.

## Features

- Bluetooth Low Energy (BLE) peripheral implementation
- Custom LED service with toggle characteristic
- LED can be toggled by writing to the characteristic

## Hardware Requirements

- Board with BLE support (configured for manikin_mainboard_portenta)
- LED connected to the pin specified by the `led0` alias in the device tree

## Building and Running

1. Ensure Zephyr SDK is installed and properly configured
2. Run the build script:

```bash
./build.sh
```

3. Flash the application:

```bash
west flash
```

## Using the Application

After flashing the application:

1. The device will start advertising as "LED Toggle"
2. Connect to the device using a BLE scanner app on your phone or computer
3. Look for the LED service (UUID: 12345678-1234-5678-1234-56789abcdef0)
4. Write any value to the toggle characteristic (UUID: 12345678-1234-5678-1234-56789abcdef1)
5. The LED should toggle state (ON/OFF) each time you write to the characteristic

## Suggested BLE Client Apps

- **Android**: nRF Connect, BLE Scanner
- **iOS**: LightBlue, nRF Connect
- **Desktop**: nRF Connect for Desktop

## Customization

To modify the application:

- Change the BLE device name in `prj.conf`
- Update the UUIDs in `led_service.c` if needed
- Modify the LED pin configuration if using a different board