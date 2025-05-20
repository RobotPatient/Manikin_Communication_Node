#!/bin/bash
# Clean and rebuild script for Manikin Communication Node

echo "==================================================================="
echo "             CLEAN BUILD FOR MANIKIN COMMUNICATION NODE            "
echo "==================================================================="
echo 
echo "Cleaning build directory..."
rm -rf build

echo "Displaying prj.conf configuration for reference:"
echo "-------------------------------------------------------------------"
cat prj.conf
echo "-------------------------------------------------------------------"
echo

echo "Building with clean configuration..."
echo "Board: manikin_mainboard_portenta/stm32h747xx/m7"
echo "Overlay: gpio.overlay"
echo

west build -p auto -b manikin_mainboard_portenta/stm32h747xx/m7 -- \
  -DBOARD_ROOT="/Users/jakorten/Development/Manikin_Communication_Node" \
  -DEXTRA_DTC_OVERLAY_FILE="/Users/jakorten/Development/Manikin_Communication_Node/overlay/manikin_mainboard_portenta/gpio.overlay" \
  -DCONFIG_DEBUG_OPTIMIZATIONS=y \
  -DCONFIG_DEBUG_THREAD_INFO=y

BUILD_RESULT=$?

# If build successful, provide flash instructions
if [ $BUILD_RESULT -eq 0 ]; then
  echo
  echo "==================================================================="
  echo "                        BUILD SUCCESSFUL!                          "
  echo "==================================================================="
  echo
  echo "To flash the application, run:"
  echo "west flash"
else
  echo
  echo "==================================================================="
  echo "                          BUILD FAILED!                            "
  echo "==================================================================="
  echo
  echo "Try running: ./minimal_build.sh to see if a minimal configuration works"
  echo "Or run: ./reset_repo.sh to reset to a known good state"
fi

exit $BUILD_RESULT