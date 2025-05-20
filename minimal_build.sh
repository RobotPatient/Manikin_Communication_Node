#!/bin/bash
# Build with minimal configuration for debugging build issues

echo "Cleaning build directory..."
rm -rf build

echo "Building with minimal configuration (no BLE)..."
west build -p auto -b manikin_mainboard_portenta/stm32h747xx/m7 -- \
  -DBOARD_ROOT="/Users/jakorten/Development/Manikin_Communication_Node" \
  -DEXTRA_DTC_OVERLAY_FILE="/Users/jakorten/Development/Manikin_Communication_Node/overlay/manikin_mainboard_portenta/gpio.overlay" \
  -DCONFIG_DEBUG_OPTIMIZATIONS=y \
  -DCONFIG_DEBUG_THREAD_INFO=y \
  -DCONF_FILE=prj_minimal.conf

# If build successful, provide flash instructions
if [ $? -eq 0 ]; then
  echo "Build successful!"
  echo "To flash the application, run:"
  echo "west flash"
else
  echo "Build failed. Please check error messages above."
fi