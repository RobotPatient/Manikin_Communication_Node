#!/bin/bash
# Build script for LED Toggle BLE application

# Clean previous build
rm -rf build

# Build for manikin_mainboard_portenta
west build -p auto -b manikin_mainboard_portenta .

# If build successful, provide flash instructions
if [ $? -eq 0 ]; then
  echo "Build successful!"
  echo "To flash the application, run:"
  echo "west flash"
fi