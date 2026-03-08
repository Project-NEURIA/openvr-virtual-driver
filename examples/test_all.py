"""
Integration test for ovd_client — requires SteamVR running with the virtual driver.

Use `uv sync --extra test` to install test dependencies
To run: cd client && uv run python ../examples/test_all.py
"""


import math
import os
import struct
import time
import sys

from ovd_client import Client, Pose, Frame, Camera

PASS = 0
FAIL = 0


def report(name: str, passed: bool, detail: str = ""):
    global PASS, FAIL
    status = "PASS" if passed else "FAIL"
    if passed:
        PASS += 1
    else:
        FAIL += 1
    msg = f"  [{status}] {name}"
    if detail:
        msg += f" — {detail}"
    print(msg)


def test_pose_dataclass():
    print("\n=== Pose ===")

    # Default is null
    p = Pose()
    report("Default pose is null", p.is_null())

    # Identity pose
    p = Pose.identity(1.0, 2.0, 3.0)
    report("Identity pose not null", not p.is_null())
    report("Identity rot_w == 1", p.rot_w == 1.0)
    report("Identity position", p.pos_x == 1.0 and p.pos_y == 2.0 and p.pos_z == 3.0)

    # Pack produces 28 bytes (7 floats)
    packed = p.pack()
    report("Pack size is 28 bytes", len(packed) == 28)

    # Round-trip pack/unpack
    vals = struct.unpack("<7f", packed)
    report("Pack round-trip", vals == (1.0, 2.0, 3.0, 1.0, 0.0, 0.0, 0.0))

    # Custom pose
    p = Pose(pos_x=0.5, pos_y=1.7, pos_z=-0.3, rot_w=0.707, rot_y=0.707)
    report("Custom pose not null", not p.is_null())
    report("Custom pose values", p.pos_y == 1.7 and abs(p.rot_w - 0.707) < 1e-6)


def test_camera():
    print("\n=== Camera ===")

    cam = Camera()
    K = cam.intrinsics
    report("Intrinsics shape (3,3)", K.shape == (3, 3))
    report("Intrinsics K[2,2] == 1", K[2, 2] == 1.0)
    report("Intrinsics fx > 0", K[0, 0] > 0)
    report("Intrinsics fy > 0", K[1, 1] > 0)

    # Intrinsics is a copy (immutable)
    K2 = cam.intrinsics
    K2[0, 0] = -999
    report("Intrinsics returns copy", cam.intrinsics[0, 0] != -999)

    # Extrinsics with identity pose
    pose = Pose.identity()
    T_left = cam.extrinsics(pose, eye=0)
    T_right = cam.extrinsics(pose, eye=1)
    report("Extrinsics shape (4,4)", T_left.shape == (4, 4))
    report("Left/right extrinsics differ", not (T_left == T_right).all(),
           "IPD offset should produce different transforms")

    # Custom resolution camera
    cam2 = Camera(render_width=2560, render_height=1440, ipd=0.065)
    K2 = cam2.intrinsics
    report("Custom camera intrinsics", K2[0, 0] != K[0, 0], "Different resolution -> different fx")


def test_connection(client: Client):
    print("\n=== Connection ===")
    report("Client connected", client._socket is not None)
    report("Socket is open", client._socket is not None and client._socket.fileno() != -1)


def test_update_pose(client: Client):
    print("\n=== update_pose ===")

    # Head only
    try:
        client.update_pose(head=Pose.identity(0, 1.7, 0))
        report("Head-only pose", True)
    except Exception as e:
        report("Head-only pose", False, str(e))

    # Full body (all 13 tracked points)
    try:
        client.update_pose(
            head=Pose.identity(0, 1.7, 0),
            left_hand=Pose.identity(-0.5, 1.0, -0.3),
            right_hand=Pose.identity(0.5, 1.0, -0.3),
            waist=Pose.identity(0, 1.0, 0),
            chest=Pose.identity(0, 1.3, 0),
            left_foot=Pose.identity(-0.15, 0.0, 0),
            right_foot=Pose.identity(0.15, 0.0, 0),
            left_knee=Pose.identity(-0.15, 0.5, 0),
            right_knee=Pose.identity(0.15, 0.5, 0),
            left_elbow=Pose.identity(-0.4, 1.2, 0),
            right_elbow=Pose.identity(0.4, 1.2, 0),
            left_shoulder=Pose.identity(-0.2, 1.5, 0),
            right_shoulder=Pose.identity(0.2, 1.5, 0),
        )
        report("Full body pose", True)
    except Exception as e:
        report("Full body pose", False, str(e))

    # Null poses (should be skipped by driver)
    try:
        client.update_pose()  # All None -> all null
        report("All-null pose (no trackers)", True)
    except Exception as e:
        report("All-null pose", False, str(e))

    # Partial body
    try:
        client.update_pose(
            head=Pose.identity(0, 1.7, 0),
            left_hand=Pose.identity(-0.5, 1.0, -0.3),
        )
        report("Partial pose (head + left hand)", True)
    except Exception as e:
        report("Partial pose", False, str(e))

    # Pose with rotation (45-degree yaw)
    yaw = math.radians(45)
    try:
        client.update_pose(
            head=Pose(
                pos_x=0, pos_y=1.7, pos_z=0,
                rot_w=math.cos(yaw / 2), rot_y=math.sin(yaw / 2),
            )
        )
        report("Rotated head pose", True)
    except Exception as e:
        report("Rotated head pose", False, str(e))


def test_update_controller(client: Client):
    print("\n=== update_controller ===")

    # Default (all zeros/false)
    try:
        client.update_controller()
        report("Default controller state", True)
    except Exception as e:
        report("Default controller state", False, str(e))

    # Trigger
    try:
        client.update_controller(trigger=1.0, trigger_click=True, trigger_touch=True)
        report("Trigger full press", True)
    except Exception as e:
        report("Trigger full press", False, str(e))

    # Grip
    try:
        client.update_controller(grip=0.75, grip_click=False, grip_touch=True)
        report("Grip partial + touch", True)
    except Exception as e:
        report("Grip partial + touch", False, str(e))

    # Joystick
    try:
        client.update_controller(joystick_x=0.5, joystick_y=-0.8, joystick_touch=True)
        report("Joystick analog + touch", True)
    except Exception as e:
        report("Joystick analog + touch", False, str(e))

    # Buttons
    try:
        client.update_controller(a_click=True, a_touch=True)
        report("A button click + touch", True)
    except Exception as e:
        report("A button", False, str(e))

    try:
        client.update_controller(b_click=True, b_touch=True)
        report("B button click + touch", True)
    except Exception as e:
        report("B button", False, str(e))

    try:
        client.update_controller(system_click=True)
        report("System button", True)
    except Exception as e:
        report("System button", False, str(e))

    try:
        client.update_controller(menu_click=True)
        report("Menu button", True)
    except Exception as e:
        report("Menu button", False, str(e))

    # Right controller aim
    try:
        client.update_controller(right_yaw=0.5, right_pitch=-0.3)
        report("Right controller aim", True)
    except Exception as e:
        report("Right controller aim", False, str(e))

    # All inputs at once
    try:
        client.update_controller(
            joystick_x=1.0, joystick_y=1.0, joystick_click=True, joystick_touch=True,
            trigger=1.0, trigger_click=True, trigger_touch=True,
            grip=1.0, grip_click=True, grip_touch=True,
            a_click=True, a_touch=True, b_click=True, b_touch=True,
            system_click=True, menu_click=True,
            right_yaw=1.0, right_pitch=1.0,
        )
        report("All inputs simultaneously", True)
    except Exception as e:
        report("All inputs simultaneously", False, str(e))

    # Reset to neutral
    client.update_controller()


def test_frame_stream(client: Client):
    print("\n=== Frame Streaming ===")

    # Manual start/stop
    try:
        client.start_frame_stream()
        frame = client.get_frame()
        client.stop_frame_stream()
        report("Manual start/get/stop", True)
        report("Frame has valid dimensions", frame.width > 0 and frame.height > 0,
               f"{frame.width}x{frame.height}")
        report("Frame eye is 0 or 1", frame.eye in (0, 1), f"eye={frame.eye}")
        report("Frame has pixel data", len(frame.data) > 0, f"{len(frame.data)} bytes")
        expected_size = frame.width * frame.height * 4  # BGRA
        report("Pixel data size matches BGRA", len(frame.data) == expected_size,
               f"expected={expected_size}, got={len(frame.data)}")
        report("Frame pose is not null", not frame.pose.is_null())
    except Exception as e:
        report("Manual start/get/stop", False, str(e))
        return  # Can't continue frame tests

    # Context manager frame_stream
    try:
        frame_count = 0
        with client.frame_stream() as frames:
            for frame in frames:
                frame_count += 1
                if frame_count >= 5:
                    break
        report("Context manager frame_stream", True, f"received {frame_count} frames")
    except Exception as e:
        report("Context manager frame_stream", False, str(e))

    # Receive both eyes
    try:
        eyes_seen = set()
        with client.frame_stream() as frames:
            for frame in frames:
                eyes_seen.add(frame.eye)
                if len(eyes_seen) == 2:
                    break
        report("Received both eyes (L+R)", len(eyes_seen) == 2, f"eyes={eyes_seen}")
    except Exception as e:
        report("Both eyes", False, str(e))


def test_camera_with_frames(client: Client):
    print("\n=== Camera intrinsics/extrinsics via Client ===")

    try:
        with client.frame_stream() as frames:
            frame = next(frames)

        # After get_frame(), camera is auto-calibrated to actual resolution
        K = client.get_intrinsics()
        report("Client.get_intrinsics shape", K.shape == (3, 3))
        report("Intrinsics match frame resolution",
               K[0, 0] == frame.width / 2.0 and K[1, 1] == frame.height / 2.0,
               f"fx={K[0,0]}, fy={K[1,1]}, frame={frame.width}x{frame.height}")

        T = client.get_extrinsics(frame)
        report("Client.get_extrinsics shape", T.shape == (4, 4))
        report("Extrinsics last row is [0,0,0,1]",
               list(T[3]) == [0.0, 0.0, 0.0, 1.0])
    except Exception as e:
        report("Camera intrinsics/extrinsics", False, str(e))


def test_save_frame(client: Client):
    print("\n=== Save Frame ===")

    try:
        import numpy as np
        from PIL import Image

        with client.frame_stream() as frames:
            frame = next(frames)

        arr = np.frombuffer(frame.data, dtype=np.uint8).reshape(frame.height, frame.width, 4)
        img = Image.fromarray(arr, "RGBA")
        out_dir = os.path.dirname(os.path.abspath(__file__))
        out_path = os.path.join(out_dir, "frame.png")
        img.save(out_path)
        report("Save frame to PNG", True, out_path)
    except Exception as e:
        report("Save frame to PNG", False, str(e))


def test_pose_movement(client: Client):
    print("\n=== Pose movement sequence ===")

    # Send a series of head poses simulating movement
    try:
        steps = 10
        for i in range(steps):
            t = i / steps
            x = math.sin(t * math.pi * 2) * 0.5
            z = math.cos(t * math.pi * 2) * 0.5 - 0.5
            yaw = t * math.pi * 2
            client.update_pose(
                head=Pose(
                    pos_x=x, pos_y=1.7, pos_z=z,
                    rot_w=math.cos(yaw / 2), rot_y=math.sin(yaw / 2),
                )
            )
            time.sleep(0.05)
        report(f"Smooth head movement ({steps} steps)", True)
    except Exception as e:
        report("Smooth head movement", False, str(e))


def test_rapid_updates(client: Client):
    print("\n=== Rapid updates ===")

    # Rapidly alternate controller states
    try:
        for i in range(50):
            client.update_controller(trigger=float(i % 2))
        report("50 rapid controller updates", True)
    except Exception as e:
        report("Rapid controller updates", False, str(e))

    # Rapidly update pose
    try:
        for i in range(50):
            client.update_pose(head=Pose.identity(0, 1.7 + (i % 10) * 0.01, 0))
        report("50 rapid pose updates", True)
    except Exception as e:
        report("Rapid pose updates", False, str(e))


def test_reconnection():
    print("\n=== Reconnection ===")

    # Connect, disconnect, reconnect
    try:
        c = Client()
        c.connect()
        report("First connect", c._socket is not None)
        c.disconnect()
        report("Disconnect", c._socket is None)
        c.connect()
        report("Reconnect", c._socket is not None)
        # Verify still functional after reconnect
        c.update_pose(head=Pose.identity(0, 1.7, 0))
        report("Functional after reconnect", True)
        c.disconnect()
    except Exception as e:
        report("Reconnection", False, str(e))


def test_context_manager():
    print("\n=== Context manager ===")

    try:
        with Client() as c:
            report("Context manager enter", c._socket is not None)
            c.update_pose(head=Pose.identity(0, 1.7, 0))
            report("Operations inside context", True)
        report("Context manager exit (auto disconnect)", c._socket is None)
    except Exception as e:
        report("Context manager", False, str(e))


def run_test(name: str, fn, *args):
    """Run a test function in its own connection, isolating failures."""
    try:
        with Client() as client:
            fn(client, *args)
    except ConnectionRefusedError:
        print(f"\n  WARN: Could not connect for {name}")
    except Exception as e:
        print(f"\n  WARN: {name} connection error: {e}")


def main():
    print("ovd_client integration test")
    print("=" * 50)
    print("Requires: SteamVR running with virtual driver\n")

    # Offline tests (no connection needed)
    test_pose_dataclass()
    test_camera()

    # Connection tests
    test_context_manager()
    test_reconnection()

    # Online tests — each in its own connection for isolation
    try:
        Client().connect()  # quick check that driver is reachable
    except ConnectionRefusedError:
        print("\nERROR: Could not connect to driver. Is SteamVR running?")
        sys.exit(1)

    run_test("Connection", test_connection)
    run_test("update_pose", test_update_pose)
    run_test("update_controller", test_update_controller)
    run_test("Pose movement", test_pose_movement)
    run_test("Rapid updates", test_rapid_updates)
    run_test("Frame Streaming", test_frame_stream)
    run_test("Camera with frames", test_camera_with_frames)
    run_test("Save frame", test_save_frame)

    # Summary
    total = PASS + FAIL
    print("\n" + "=" * 50)
    print(f"Results: {PASS}/{total} passed, {FAIL} failed")
    sys.exit(0 if FAIL == 0 else 1)


if __name__ == "__main__":
    main()
