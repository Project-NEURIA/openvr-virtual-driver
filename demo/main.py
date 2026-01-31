import socket
import struct
import math
import time
import os
import sys
import numpy as np
import pygame

# Add client/src to path for VMDPlayer import
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'client', 'src'))
from vmd import VMDPlayer


# Protocol:
# MsgHeader (8 bytes): type (u32) | size (u32)
# Frame (type=0): width (u32) | height (u32) | eye (u32) | pixels (width * height * 4 bytes)
# BodyPosition (type=1): 13 Pose structs (head + 12 body parts, 7 floats each = 364 bytes)
# Controller (type=2): joystick_x, joystick_y (floats), joystick_click, joystick_touch,
#                      trigger (float), trigger_click, trigger_touch,
#                      grip (float), grip_click, grip_touch,
#                      a_click, a_touch, b_click, b_touch, system_click, menu_click (all uint8),
#                      right_yaw, right_pitch (floats)
#
# All positions are absolute world coordinates. Clients should send BodyPosition and Controller
# at 60-90Hz for smooth tracking. The driver updates SteamVR immediately upon receiving data.

MSG_HEADER_SIZE = 8
FRAME_INFO_SIZE = 12
MSG_TYPE_FRAME = 0
MSG_TYPE_BODY_POSITION = 1
MSG_TYPE_CONTROLLER = 2

# Pose struct: posX, posY, posZ, rotW, rotX, rotY, rotZ (7 floats = 28 bytes)
POSE_SIZE = 28
# BodyPosition: 13 poses (head, leftHand, rightHand, waist, chest, leftFoot, rightFoot, leftKnee, rightKnee, leftElbow, rightElbow, leftShoulder, rightShoulder)
BODY_POSITION_SIZE = POSE_SIZE * 13

HOST = "127.0.0.1"
PORT = 21213

# Mouse sensitivity
SENSITIVITY = 0.002
# Movement speed (meters per frame)
MOVE_SPEED = 0.05


def receive_exact(sock: socket.socket, size: int) -> bytes:
    """Receive exactly `size` bytes from socket."""
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("Connection closed")
        data += chunk
    return data


def send_controller(sock: socket.socket,
                    joystick_x: float, joystick_y: float, joystick_click: bool, joystick_touch: bool,
                    trigger: float, trigger_click: bool, trigger_touch: bool,
                    grip: float, grip_click: bool, grip_touch: bool,
                    a_click: bool, a_touch: bool, b_click: bool, b_touch: bool,
                    system_click: bool, menu_click: bool,
                    right_yaw: float, right_pitch: float):
    """Send a controller input message."""
    controller_data = struct.pack("<ff BB f BB f BB BBBBBB ff",
        joystick_x, joystick_y, joystick_click, joystick_touch,
        trigger, trigger_click, trigger_touch,
        grip, grip_click, grip_touch,
        a_click, a_touch, b_click, b_touch, system_click, menu_click,
        right_yaw, right_pitch)
    header = struct.pack("<II", MSG_TYPE_CONTROLLER, len(controller_data))
    sock.sendall(header + controller_data)


def pack_pose(pos_x: float, pos_y: float, pos_z: float, rot_w: float, rot_x: float, rot_y: float, rot_z: float) -> bytes:
    """Pack a single pose (position + quaternion rotation) into bytes."""
    return struct.pack("<7f", pos_x, pos_y, pos_z, rot_w, rot_x, rot_y, rot_z)


def send_body_position(sock: socket.socket, body_pos: dict):
    """Send a body position message.

    body_pos should be a dict with keys:
    'head', 'left_hand', 'right_hand', 'waist', 'chest', 'left_foot', 'right_foot',
    'left_knee', 'right_knee', 'left_elbow', 'right_elbow', 'left_shoulder', 'right_shoulder'

    Each value should be a tuple of (pos_x, pos_y, pos_z, rot_w, rot_x, rot_y, rot_z)
    """
    pose_order = [
        'head', 'left_hand', 'right_hand', 'waist', 'chest',
        'left_foot', 'right_foot', 'left_knee', 'right_knee',
        'left_elbow', 'right_elbow', 'left_shoulder', 'right_shoulder'
    ]

    body_data = b''
    for pose_name in pose_order:
        pose = body_pos.get(pose_name, (0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0))
        body_data += pack_pose(*pose)

    header = struct.pack("<II", MSG_TYPE_BODY_POSITION, len(body_data))
    sock.sendall(header + body_data)


def euler_to_quaternion(yaw: float, pitch: float):
    """Convert yaw (around Y) and pitch (around X) to quaternion for OpenVR."""
    # Quaternion for yaw (rotation around Y axis)
    cy = math.cos(yaw * 0.5)
    sy = math.sin(yaw * 0.5)

    # Quaternion for pitch (rotation around X axis)
    cp = math.cos(pitch * 0.5)
    sp = math.sin(pitch * 0.5)

    # Combine: first yaw, then pitch (q_pitch * q_yaw)
    qw = cp * cy
    qx = sp * cy
    qy = cp * sy
    qz = -sp * sy

    return qw, qx, qy, qz


def main():
    print(f"Connecting to {HOST}:{PORT}...")

    conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    conn.connect((HOST, PORT))
    print("Connected!")

    # Initialize pygame
    pygame.init()
    screen = pygame.display.set_mode((960, 540), pygame.RESIZABLE)
    pygame.display.set_caption("VR View")

    # Enable relative mouse mode for infinite movement
    pygame.mouse.set_relative_mode(True)

    # Load VMD file if it exists
    vmd_player = None
    vmd_path = os.path.join(os.path.dirname(__file__), "PV058_MIK_M2_WIM.vmd")
    if os.path.exists(vmd_path):
        try:
            vmd_player = VMDPlayer(vmd_path, fps=30.0)
            print(f"VMD loaded: {vmd_path}")
        except Exception as e:
            print(f"Failed to load VMD: {e}")

    # Load audio file if it exists
    audio_loaded = False
    audio_path = os.path.join(os.path.dirname(__file__), "PV058_MIX.wav")
    if os.path.exists(audio_path):
        try:
            pygame.mixer.init()
            pygame.mixer.music.load(audio_path)
            audio_loaded = True
            print(f"Audio loaded: {audio_path}")
        except Exception as e:
            print(f"Failed to load audio: {e}")

    print("Mouse captured. Move mouse to look around. WASD to move.")
    print("1=Trigger, 2=Grip, 3=A, 4=B, 5=Joystick click, 6=Menu.")
    print("Hold ` (backtick) + mouse = aim right controller. ESC to quit.")
    print("P = Play/Pause VMD, R = Reset. T-pose sent by default.")

    # Position and rotation
    pos_x, pos_y, pos_z = 0.0, 1.7, 0.0
    yaw, pitch = 0.0, 0.0

    # Right controller rotation
    right_yaw, right_pitch = 0.0, 0.0

    # Send initial T-pose and HMD position
    send_controller(conn,
        joystick_x=0.0, joystick_y=0.0, joystick_click=False, joystick_touch=False,
        trigger=0.0, trigger_click=False, trigger_touch=False,
        grip=0.0, grip_click=False, grip_touch=False,
        a_click=False, a_touch=False, b_click=False, b_touch=False,
        system_click=False, menu_click=False,
        right_yaw=0.0, right_pitch=0.0)
    initial_body_pos = {
        'head': (pos_x, pos_y, pos_z, 1.0, 0.0, 0.0, 0.0),
        'waist': (pos_x, 0.93, pos_z, 1.0, 0.0, 0.0, 0.0),
        'chest': (pos_x, 1.29, pos_z, 1.0, 0.0, 0.0, 0.0),
        'left_shoulder': (pos_x - 0.15, 1.41, pos_z, 1.0, 0.0, 0.0, 0.0),
        'right_shoulder': (pos_x + 0.15, 1.41, pos_z, 1.0, 0.0, 0.0, 0.0),
        'left_elbow': (pos_x - 0.45, 1.41, pos_z, 1.0, 0.0, 0.0, 0.0),
        'right_elbow': (pos_x + 0.45, 1.41, pos_z, 1.0, 0.0, 0.0, 0.0),
        'left_hand': (pos_x - 0.67, 1.41, pos_z, 1.0, 0.0, 0.0, 0.0),
        'right_hand': (pos_x + 0.67, 1.41, pos_z, 1.0, 0.0, 0.0, 0.0),
        'left_knee': (pos_x - 0.09, 0.46, pos_z, 1.0, 0.0, 0.0, 0.0),
        'right_knee': (pos_x + 0.09, 0.46, pos_z, 1.0, 0.0, 0.0, 0.0),
        'left_foot': (pos_x - 0.09, 0.06, pos_z, 1.0, 0.0, 0.0, 0.0),
        'right_foot': (pos_x + 0.09, 0.06, pos_z, 1.0, 0.0, 0.0, 0.0),
    }
    send_body_position(conn, initial_body_pos)
    print("Initial T-pose sent.")

    # Animation timing
    last_time = time.time()

    running = True
    try:
        while running:
            # Calculate delta time for animation
            current_time = time.time()
            delta_time = current_time - last_time
            last_time = current_time

            # Track if position changed this frame (for T-pose body sync)
            position_changed = False

            # Handle pygame events
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        running = False
                    elif event.key == pygame.K_p and vmd_player:
                        playing = vmd_player.toggle()
                        # Sync audio with VMD
                        if audio_loaded:
                            if playing:
                                # Resume or start from current position
                                pos_ms = int(vmd_player.current_frame / vmd_player.fps * 1000)
                                pygame.mixer.music.play(start=pos_ms / 1000.0)
                            else:
                                pygame.mixer.music.pause()
                        print(f"VMD {'Playing' if playing else 'Paused'} at frame {vmd_player.current_frame:.0f}")
                    elif event.key == pygame.K_r and vmd_player:
                        vmd_player.reset()
                        if audio_loaded:
                            pygame.mixer.music.stop()
                        print("VMD Reset to frame 0")
                elif event.type == pygame.MOUSEMOTION:
                    # Get relative mouse movement
                    dx, dy = event.rel

                    if dx != 0 or dy != 0:
                        # Check if backtick is held - control right hand instead of head
                        keys_now = pygame.key.get_pressed()
                        if keys_now[pygame.K_BACKQUOTE]:
                            # Control right hand
                            right_yaw -= dx * SENSITIVITY
                            right_pitch -= dy * SENSITIVITY

                            # Clamp pitch
                            right_pitch = max(-math.pi / 2, min(math.pi / 2, right_pitch))

                            print(f"Right hand: yaw={math.degrees(right_yaw):.1f}° pitch={math.degrees(right_pitch):.1f}°")
                        else:
                            # Control head
                            yaw -= dx * SENSITIVITY
                            pitch -= dy * SENSITIVITY

                            # Clamp pitch to avoid gimbal lock
                            pitch = max(-math.pi / 2 + 0.01, min(math.pi / 2 - 0.01, pitch))
                            position_changed = True

                            print(f"Rotation: yaw={math.degrees(yaw):.1f}° pitch={math.degrees(pitch):.1f}°")

            # Handle WASD movement
            keys = pygame.key.get_pressed()
            move_x, move_z = 0.0, 0.0

            if keys[pygame.K_w]:
                move_z += 1.0
            if keys[pygame.K_s]:
                move_z -= 1.0
            if keys[pygame.K_a]:
                move_x -= 1.0
            if keys[pygame.K_d]:
                move_x += 1.0

            if move_x != 0.0 or move_z != 0.0:
                # Normalize diagonal movement
                length = math.sqrt(move_x * move_x + move_z * move_z)
                move_x /= length
                move_z /= length

                # Rotate movement by yaw (move relative to look direction)
                cos_yaw = math.cos(yaw)
                sin_yaw = math.sin(yaw)
                world_x = move_x * cos_yaw - move_z * sin_yaw
                world_z = move_x * sin_yaw + move_z * cos_yaw

                pos_x += world_x * MOVE_SPEED
                pos_z -= world_z * MOVE_SPEED
                position_changed = True

            # Handle controller inputs
            # Key mappings:
            # 1 - Trigger, 2 - Grip
            # 3 - A button, 4 - B button
            # 5 - Joystick click, 6 - Menu
            # Arrow keys - right controller aim
            trigger = 1.0 if keys[pygame.K_1] else 0.0
            grip = 1.0 if keys[pygame.K_2] else 0.0
            a_click = keys[pygame.K_3]
            b_click = keys[pygame.K_4]
            joystick_click = keys[pygame.K_5]
            menu_click = keys[pygame.K_6]


            send_controller(conn,
                joystick_x=0.0, joystick_y=0.0, joystick_click=joystick_click, joystick_touch=False,
                trigger=trigger, trigger_click=(trigger > 0.9), trigger_touch=(trigger > 0.0),
                grip=grip, grip_click=(grip > 0.9), grip_touch=(grip > 0.0),
                a_click=a_click, a_touch=a_click,
                b_click=b_click, b_touch=b_click,
                system_click=False, menu_click=menu_click,
                right_yaw=right_yaw, right_pitch=right_pitch)

            # Send body pose: VMD (playing or paused) or T-pose if no VMD
            # base_position is ground level XZ, skeleton has its own heights
            if vmd_player and vmd_player.current_frame > 0 or (vmd_player and vmd_player.playing):
                # Advance only if playing
                if vmd_player.playing:
                    frames_to_advance = delta_time * vmd_player.fps
                    vmd_player.advance_frame(frames_to_advance)

                # Get head transform for HMD
                hx, hy, hz, hw, hqx, hqy, hqz = vmd_player.get_head_transform(base_position=(pos_x, 0.0, pos_z))

                # Get body pose for trackers (current frame, even if paused)
                body_pos = vmd_player.get_body_pose(base_position=(pos_x, 0.0, pos_z))
                body_pos['head'] = (hx, hy, hz, hw, hqx, hqy, hqz)
                send_body_position(conn, body_pos)
            elif position_changed:
                # Send T-pose only when position/rotation changed
                # Hip at 0.93m, chest higher, shorter upper arms
                # Rotate body with yaw
                cos_yaw = math.cos(yaw)
                sin_yaw = math.sin(yaw)

                def rotated_pos(offset_x, height, offset_z=0.0):
                    """Rotate offset by yaw and add to player position."""
                    rx = offset_x * cos_yaw - offset_z * sin_yaw
                    rz = offset_x * sin_yaw + offset_z * cos_yaw
                    return (pos_x + rx, height, pos_z + rz)

                # Head rotation quaternion (yaw + pitch)
                head_qw, head_qx, head_qy, head_qz = euler_to_quaternion(yaw, pitch)
                # Body rotation quaternion (yaw only)
                qw, qx, qy, qz = euler_to_quaternion(yaw, 0.0)

                body_pos = {
                    'head': (pos_x, pos_y, pos_z, head_qw, head_qx, head_qy, head_qz),
                    'waist': (*rotated_pos(0.0, 0.93), qw, qx, qy, qz),
                    'chest': (*rotated_pos(0.0, 1.29), qw, qx, qy, qz),
                    'left_shoulder': (*rotated_pos(-0.15, 1.41), qw, qx, qy, qz),
                    'right_shoulder': (*rotated_pos(0.15, 1.41), qw, qx, qy, qz),
                    'left_elbow': (*rotated_pos(-0.45, 1.41), qw, qx, qy, qz),
                    'right_elbow': (*rotated_pos(0.45, 1.41), qw, qx, qy, qz),
                    'left_hand': (*rotated_pos(-0.67, 1.41), qw, qx, qy, qz),
                    'right_hand': (*rotated_pos(0.67, 1.41), qw, qx, qy, qz),
                    'left_knee': (*rotated_pos(-0.09, 0.46), qw, qx, qy, qz),
                    'right_knee': (*rotated_pos(0.09, 0.46), qw, qx, qy, qz),
                    'left_foot': (*rotated_pos(-0.09, 0.06), qw, qx, qy, qz),
                    'right_foot': (*rotated_pos(0.09, 0.06), qw, qx, qy, qz),
                }
                send_body_position(conn, body_pos)

            # Read message header
            msg_header = receive_exact(conn, MSG_HEADER_SIZE)
            msg_type, msg_size = struct.unpack("<II", msg_header)

            if msg_type == MSG_TYPE_FRAME:
                # Read frame info
                frame_info = receive_exact(conn, FRAME_INFO_SIZE)
                width, height, eye = struct.unpack("<III", frame_info)

                # Read pixel data
                pixel_size = msg_size - FRAME_INFO_SIZE
                pixel_data = receive_exact(conn, pixel_size)

                # Only display left eye (eye == 0)
                if eye == 0:
                    # Convert to numpy array (BGRA format)
                    frame = np.frombuffer(pixel_data, dtype=np.uint8).reshape((height, width, 4))

                    # Convert BGRA to RGB for pygame
                    rgb_frame = frame[:, :, [2, 1, 0]]

                    # Create pygame surface and display
                    surface = pygame.surfarray.make_surface(rgb_frame.swapaxes(0, 1))

                    # Scale to fit window
                    window_size = screen.get_size()
                    scaled_surface = pygame.transform.scale(surface, window_size)
                    screen.blit(scaled_surface, (0, 0))
                    pygame.display.flip()

    except ConnectionError as e:
        print(f"Connection ended: {e}")
    except KeyboardInterrupt:
        print("Interrupted")
    finally:
        if audio_loaded:
            pygame.mixer.music.stop()
            pygame.mixer.quit()
        pygame.mouse.set_relative_mode(False)
        pygame.quit()
        conn.close()


if __name__ == "__main__":
    main()
