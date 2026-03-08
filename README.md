### Driver Installation
1. Install SteamVR on Steam
2. Find the latest release on the Releases page, download and extract openvr-virtual-driver-vx.x.x.zip
3. Double-click `install.bat`

The installer automatically detects your SteamVR location and installs the driver.

### Reinstalling / Updating the Driver
1. Download the new release and extract it
2. Double-click `install.bat` — it will replace the existing installation

### Uninstalling the Driver
1. Double-click `uninstall.bat` from any release, or manually delete the `openvr_virtual_driver` folder from your SteamVR `drivers` directory

### Building from Source
Prerequisites: CMake >= 3.16, a C++23 compatible compiler (MSVC), Git

1. Clone the repository with submodules:
   ```sh
   git clone --recurse-submodules https://github.com/Project-NEURIA/openvr-virtual-driver.git
   cd openvr-virtual-driver
   ```
2. Build:
   ```sh
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```
3. The compiled driver will be output to `openvr_virtual_driver/bin/win64/`. Run `install.bat` to install it to SteamVR.

### Python Client
1. In a new python project do `uv add openvr-virtual-driver-client` or `pip install openvr-virtual-driver-client`
2. Start a SteamVR game
3. Run your python project, see examples directory for examples
