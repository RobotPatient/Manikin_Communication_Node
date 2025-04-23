# Manikin_Communication_Node
Manikin Communication Node

## How to build with vscode:

1. Download the Zephyr IDE plugin in vscode: https://marketplace.visualstudio.com/items/?itemName=mylonics.zephyr-ide-extension-pack

2. Go to the zephyr tab in your left bar, and select global

3. Follow the steps in extension setup

4. When west init is reached, select Full zephyr as a lot of packages depend on each other


## How to flash the STM32WB
To flash the stm32wb5mmg:

1. Install the stm32cubeprogrammer application

2. Download the **HCI extended Fw** and FUS firmware: 
    FW: https://github.com/STMicroelectronics/STM32CubeWB/blob/v1.20.0/Projects/STM32WB_Copro_Wireless_Binaries/STM32WB5x/stm32wb5x_BLE_HCILayer_extended_fw.bin
    FUS FW: https://github.com/STMicroelectronics/STM32CubeWB/blob/v1.20.0/Projects/STM32WB_Copro_Wireless_Binaries/STM32WB5x/stm32wb5x_FUS_fw.bin

3. Go to the fourth tab with wireless sign in stm32cubeprog

4. Load the FUS binary first at adress: 0x080EC000

5. Load the extended hci binary at adress: 0x080DA000

When in doubt look at this video: https://www.youtube.com/watch?v=1LvfBC_P6eg

6. Use the launch config in this folder to program the device or use the zephyr ide 

-> When using openocd with stlink, change the json to something like this:

```
 {
      "name": "Debug STM32WB5MMG (OpenOCD)",
      "cwd": "${workspaceFolder}",
      "executable": "${workspaceFolder}/build/zephyr/zephyr.elf",
      "request": "launch",
      "type": "cortex-debug",
      "servertype": "openocd",
      "device": "STM32WB5MMG",
      "interface": "swd",
      "svdFile": "${workspaceFolder}/STM32F405.svd",
      "showDevDebugOutput": "both",
      "configFiles": ["interface/stlink.cfg", "target/stm32wbx.cfg"],
      "openOCDLaunchCommands": ["adapter speed 4000"],
      "postLaunchCommands": ["monitor reset halt"]
    },
```