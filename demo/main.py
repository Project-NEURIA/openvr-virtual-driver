import socket
import struct
import numpy as np
import cv2


# Protocol:
# Header (12 bytes): width (u32) | height (u32) | eye (u32: 0=left, 1=right)
# Data: raw BGRA pixels (width * height * 4 bytes)

HEADER_SIZE = 12
HOST = "127.0.0.1"
PORT = 8080


def receive_exact(sock: socket.socket, size: int) -> bytes:
    """Receive exactly `size` bytes from socket."""
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("Connection closed")
        data += chunk
    return data


def main():
    print(f"Waiting for connection on {HOST}:{PORT}...")

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, PORT))
    server.listen(1)

    conn, addr = server.accept()
    print(f"Connected: {addr}")

    cv2.namedWindow("Left Eye", cv2.WINDOW_NORMAL)
    cv2.namedWindow("Right Eye", cv2.WINDOW_NORMAL)

    frame_count = 0
    try:
        while True:
            # Read header
            header = receive_exact(conn, HEADER_SIZE)
            width, height, eye = struct.unpack("<III", header)

            # Read pixel data
            data_size = width * height * 4
            pixel_data = receive_exact(conn, data_size)

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

    except ConnectionError as e:
        print(f"Connection ended: {e}")
    except KeyboardInterrupt:
        print("Interrupted")
    finally:
        conn.close()
        server.close()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
