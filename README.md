### Driver Installation
1. Install SteamVR on Steam
2. Find the latest release on the Releases page, download and extract openvr-virtual-driver-vx.x.x.zip
3. Run in powershell:
    ```powershell
    & "C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" adddriver "C:\path\to\your\openvr_virtual_driver"
    ```

### Reinstalling the Driver
1. Remove the existing driver:
   ```powershell
   & "C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" removedriver "C:\path\to\your\openvr_virtual_driver"
   ```
2. Re-add the driver using step 3 from Driver Installation above.

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
3. The compiled driver will be output to `openvr_virtual_driver/bin/win64/`. Register it with SteamVR using step 3 from Driver Installation above.

### Python Client
1. In a new python project do `uv add openvr-virtual-driver-client` or `pip install openvr-virtual-driver-client`
2. Start a SteamVR game
3. Run your python project, see examples directory for examples
