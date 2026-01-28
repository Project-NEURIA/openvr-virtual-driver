import socket
import struct
import numpy as np
import cv2


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


def main():
    print(f"Connecting to {HOST}:{PORT}...")

    conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    conn.connect((HOST, PORT))
    print("Connected!")

    cv2.namedWindow("Left Eye", cv2.WINDOW_NORMAL)
    cv2.namedWindow("Right Eye", cv2.WINDOW_NORMAL)

    frame_count = 0
    try:
        while True:
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

                # Convert to numpy array (BGRA format, 4 bytes per pixel)
                frame = np.frombuffer(pixel_data, dtype=np.uint8).reshape((height, width, 4))

                # Debug info every 30 frames
                frame_count += 1
                if frame_count % 30 == 1:
                    print(f"Frame {frame_count}: {width}x{height}, eye={eye}")
                    print(f"  Min pixel value: {frame.min()}, Max: {frame.max()}, Mean: {frame.mean():.1f}")

                # Display (take BGR from BGRA)
                window_name = "Left Eye" if eye == 0 else "Right Eye"
                cv2.imshow(window_name, frame[:, :, :3])

            # Check for quit (press 'q' or ESC)
            key = cv2.waitKey(1) & 0xFF
            if key == ord("q") or key == 27:
                break

            # Example: send a test position (uncomment to test)
            # send_position(conn, 0.0, 1.6, 0.0, 1.0, 0.0, 0.0, 0.0)

    except ConnectionError as e:
        print(f"Connection ended: {e}")
    except KeyboardInterrupt:
        print("Interrupted")
    finally:
        conn.close()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
