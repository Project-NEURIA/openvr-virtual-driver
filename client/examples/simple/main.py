import os
import sys

# Add client/src to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "src"))
from client import Client

ASSETS_DIR = os.path.join(os.path.dirname(__file__), "assets")

with Client() as client:
    client.play(
        vmd_path=os.path.join(ASSETS_DIR, "PV058_MIK_M2_WIM.vmd"),
        audio_path=os.path.join(ASSETS_DIR, "PV058_MIX.wav"),
    )
