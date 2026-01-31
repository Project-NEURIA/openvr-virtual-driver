### Driver Installation
1. Install SteamVR on Steam
2. Find the latest release on the Releases page, download and extract openvr-virtual-driver-vx.x.x.zip
3. Run "C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" adddriver "C:\path\to\your\openvr_virtual_driver" in powershell

### Python Client
1. In a new python project do```uv add openvr-virtual-driver-client``` or ````pip install openvr-virtual-driver-client```
2. Start a steamVR game
3. Run your python project, see examples directory for examples
