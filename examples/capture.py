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
        with client.frame_stream() as frames:
            frame = next(frames)

        for eye_name, data in [("left", frame.left), ("right", frame.right)]:
            arr = np.frombuffer(data, dtype=np.uint8).reshape(
                frame.height, frame.width, 4
            )
            img = Image.fromarray(arr, "RGBA")
            out_path = os.path.join(out_dir, f"{eye_name}.png")
            img.save(out_path)
            print(f"Saved {eye_name} eye: {out_path}")

    print("Done.")


if __name__ == "__main__":
    main()
