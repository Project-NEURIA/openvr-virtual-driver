import socket
import struct
import math
import numpy as np
import pygame


# Protocol:
# MsgHeader (8 bytes): type (u32) | size (u32)
# Frame (type=0): width (u32) | height (u32) | eye (u32) | pixels (width * height * 4 bytes)
# Position (type=1): x, y, z, qw, qx, qy, qz (7 doubles = 56 bytes)

MSG_HEADER_SIZE = 8
FRAME_INFO_SIZE = 12
MSG_TYPE_FRAME = 0
MSG_TYPE_POSITION = 1

HOST = "127.0.0.1"
PORT = 21213

# Mouse sensitivity
SENSITIVITY = 0.002


def receive_exact(sock: socket.socket, size: int) -> bytes:
    """Receive exactly `size` bytes from socket."""
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("Connection closed")
        data += chunk
    return data


def send_position(sock: socket.socket, x: float, y: float, z: float, qw: float, qx: float, qy: float, qz: float):
    """Send a position message."""
    position_data = struct.pack("<7d", x, y, z, qw, qx, qy, qz)
    header = struct.pack("<II", MSG_TYPE_POSITION, len(position_data))
    sock.sendall(header + position_data)


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
    screen = pygame.display.set_mode((1920, 1080), pygame.RESIZABLE)
    pygame.display.set_caption("VR View")

    # Enable relative mouse mode for infinite movement
    pygame.mouse.set_relative_mode(True)

    print("Mouse captured. Move mouse to look around. Press ESC to quit.")

    # Position and rotation
    pos_x, pos_y, pos_z = 0.0, 1.6, 0.0
    yaw, pitch = 0.0, 0.0

    running = True
    try:
        while running:
            # Handle pygame events
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        running = False
                elif event.type == pygame.MOUSEMOTION:
                    # Get relative mouse movement
                    dx, dy = event.rel

                    if dx != 0 or dy != 0:
                        yaw -= dx * SENSITIVITY
                        pitch -= dy * SENSITIVITY

                        # Clamp pitch to avoid gimbal lock
                        pitch = max(-math.pi / 2 + 0.01, min(math.pi / 2 - 0.01, pitch))

                        # Convert to quaternion and send
                        qw, qx, qy, qz = euler_to_quaternion(yaw, pitch)
                        send_position(conn, pos_x, pos_y, pos_z, qw, qx, qy, qz)

                        print(f"Rotation: yaw={math.degrees(yaw):.1f}° pitch={math.degrees(pitch):.1f}°")

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
        pygame.mouse.set_relative_mode(False)
        pygame.quit()
        conn.close()


if __name__ == "__main__":
    main()
