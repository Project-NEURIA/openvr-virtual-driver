#!/bin/bash
set -e

DRIVER_NAME="openvr_virtual_driver"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DRIVER_SRC="$SCRIPT_DIR/$DRIVER_NAME"

# Find SteamVR drivers directory
STEAMVR_DIRS=(
    "$HOME/.steam/steam/steamapps/common/SteamVR/drivers"
    "$HOME/.local/share/Steam/steamapps/common/SteamVR/drivers"
    "$HOME/.steam/debian-installation/steamapps/common/SteamVR/drivers"
)

DRIVERS_DIR=""
for dir in "${STEAMVR_DIRS[@]}"; do
    if [ -d "$dir" ]; then
        DRIVERS_DIR="$dir"
        break
    fi
done

if [ -z "$DRIVERS_DIR" ]; then
    echo "Error: Could not find SteamVR drivers directory."
    echo "Make sure SteamVR is installed via Steam."
    exit 1
fi

echo "Installing $DRIVER_NAME to $DRIVERS_DIR/$DRIVER_NAME ..."
rm -rf "$DRIVERS_DIR/$DRIVER_NAME"
cp -r "$DRIVER_SRC" "$DRIVERS_DIR/$DRIVER_NAME"
echo "Done! Restart SteamVR to load the driver."
