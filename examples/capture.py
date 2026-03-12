"""
Capture left and right eye frames from the virtual driver.

Requires SteamVR running with the virtual driver installed.
Usage: cd client && uv run python ../examples/capture.py
"""

import os

import numpy as np
from PIL import Image

from ovd_client import Client


def main():
    out_dir = os.path.dirname(os.path.abspath(__file__))

    with Client() as client:
        saved = {}
        with client.frame_stream() as frames:
            for frame in frames:
                eye_name = "left" if frame.eye == 0 else "right"
                if eye_name not in saved:
                    arr = np.frombuffer(frame.data, dtype=np.uint8).reshape(
                        frame.height, frame.width, 4
                    )
                    img = Image.fromarray(arr, "RGBA")
                    out_path = os.path.join(out_dir, f"{eye_name}.png")
                    img.save(out_path)
                    saved[eye_name] = out_path
                    print(f"Saved {eye_name} eye: {out_path}")
                if len(saved) == 2:
                    break

    print("Done.")


if __name__ == "__main__":
    main()
