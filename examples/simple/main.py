import os

# Add client to path
from ovd_client import Client, Player

ASSETS_DIR = os.path.join(os.path.dirname(__file__), "assets")

with Client() as client:
    player = Player(client)
    player.play(
        vmd_path=os.path.join(ASSETS_DIR, "PV058_MIK_M2_WIM.vmd"),
        audio_path=os.path.join(ASSETS_DIR, "PV058_MIX.wav"),
    )
