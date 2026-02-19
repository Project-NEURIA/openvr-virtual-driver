import socket
import struct
from contextlib import contextmanager
from dataclasses import dataclass
from typing import Generator, Optional


# Protocol constants
MSG_TYPE_FRAME = 0
MSG_TYPE_BODY_POSITION = 1
MSG_TYPE_CONTROLLER = 2
MSG_TYPE_FRAME_START = 3
MSG_TYPE_FRAME_STOP = 4

MSG_HEADER_SIZE = 8
FRAME_INFO_SIZE = 12
POSE_SIZE = 28  # 7 floats
BODY_POSITION_SIZE = POSE_SIZE * 13  # head + 12 body parts

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 21213


@dataclass
class Pose:
    """Position and rotation (quaternion) for a tracked point.

    Default is a null pose (all zeros). Use Pose.identity() for a valid
    identity pose at origin.
    """
    pos_x: float = 0.0
    pos_y: float = 0.0
    pos_z: float = 0.0
    rot_w: float = 0.0
    rot_x: float = 0.0
    rot_y: float = 0.0
    rot_z: float = 0.0

    @classmethod
    def identity(cls, x: float = 0.0, y: float = 0.0, z: float = 0.0) -> "Pose":
        """Create an identity pose (no rotation) at the given position."""
        return cls(pos_x=x, pos_y=y, pos_z=z, rot_w=1.0)

    def is_null(self) -> bool:
        """Check if this pose is null (all zeros, will be skipped by driver)."""
        return (self.pos_x == 0.0 and self.pos_y == 0.0 and self.pos_z == 0.0 and
                self.rot_w == 0.0 and self.rot_x == 0.0 and self.rot_y == 0.0 and self.rot_z == 0.0)

    def pack(self) -> bytes:
        return struct.pack("<7f", self.pos_x, self.pos_y, self.pos_z,
                           self.rot_w, self.rot_x, self.rot_y, self.rot_z)


@dataclass
class Frame:
    """VR frame data received from the driver."""
    width: int
    height: int
    eye: int  # 0 = left, 1 = right
    data: bytes


class Client:
    """TCP client for communicating with the OpenVR virtual driver."""

    def __init__(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT) -> None:
        self.host = host
        self.port = port
        self._socket: Optional[socket.socket] = None

    def connect(self) -> None:
        """Connect to the driver."""
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._socket.connect((self.host, self.port))

    def disconnect(self) -> None:
        """Disconnect from the driver."""
        if self._socket:
            self._socket.close()
            self._socket = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, _exc_type, _exc_val, _exc_tb):
        self.disconnect()
        return False

    def _send(self, msg_type: int, data: bytes) -> None:
        """Send a message with header."""
        if self._socket is None:
            raise ConnectionError("Not connected")
        header = struct.pack("<II", msg_type, len(data))
        self._socket.sendall(header + data)

    def _recv_exact(self, size: int) -> bytes:
        """Receive exactly `size` bytes."""
        if self._socket is None:
            raise ConnectionError("Not connected")
        data = b""
        while len(data) < size:
            chunk = self._socket.recv(size - len(data))
            if not chunk:
                raise ConnectionError("Connection closed")
            data += chunk
        return data

    def start_frame_stream(self) -> None:
        """Tell the driver to start sending frame data to this client."""
        self._send(MSG_TYPE_FRAME_START, b"")

    def stop_frame_stream(self) -> None:
        """Tell the driver to stop sending frame data to this client."""
        self._send(MSG_TYPE_FRAME_STOP, b"")

    @contextmanager
    def frame_stream(self) -> Generator:
        """Context manager that enables frame streaming.

        Usage:
            with client.frame_stream() as frames:
                for frame in frames:
                    process(frame)
        """
        self.start_frame_stream()
        try:
            yield self._frame_iter()
        finally:
            try:
                self.stop_frame_stream()
            except Exception:
                pass

    def _frame_iter(self) -> Generator[Frame, None, None]:
        """Yield frames from the driver indefinitely."""
        while True:
            yield self.get_frame()

    def update_controller(
        self,
        joystick_x: float = 0.0,
        joystick_y: float = 0.0,
        joystick_click: bool = False,
        joystick_touch: bool = False,
        trigger: float = 0.0,
        trigger_click: bool = False,
        trigger_touch: bool = False,
        grip: float = 0.0,
        grip_click: bool = False,
        grip_touch: bool = False,
        a_click: bool = False,
        a_touch: bool = False,
        b_click: bool = False,
        b_touch: bool = False,
        system_click: bool = False,
        menu_click: bool = False,
        right_yaw: float = 0.0,
        right_pitch: float = 0.0,
    ) -> None:
        """Send controller input state."""
        data = struct.pack(
            "<ff BB f BB f BB BBBBBB ff",
            joystick_x, joystick_y,
            joystick_click, joystick_touch,
            trigger, trigger_click, trigger_touch,
            grip, grip_click, grip_touch,
            a_click, a_touch, b_click, b_touch,
            system_click, menu_click,
            right_yaw, right_pitch,
        )
        self._send(MSG_TYPE_CONTROLLER, data)

    def update_pose(
        self,
        head: Optional[Pose] = None,
        left_hand: Optional[Pose] = None,
        right_hand: Optional[Pose] = None,
        waist: Optional[Pose] = None,
        chest: Optional[Pose] = None,
        left_foot: Optional[Pose] = None,
        right_foot: Optional[Pose] = None,
        left_knee: Optional[Pose] = None,
        right_knee: Optional[Pose] = None,
        left_elbow: Optional[Pose] = None,
        right_elbow: Optional[Pose] = None,
        left_shoulder: Optional[Pose] = None,
        right_shoulder: Optional[Pose] = None,
    ) -> None:
        """Send body position. Poses set to None (or Pose() which defaults to null) will be skipped by the driver."""
        data = (
            (head or Pose()).pack() +
            (left_hand or Pose()).pack() +
            (right_hand or Pose()).pack() +
            (waist or Pose()).pack() +
            (chest or Pose()).pack() +
            (left_foot or Pose()).pack() +
            (right_foot or Pose()).pack() +
            (left_knee or Pose()).pack() +
            (right_knee or Pose()).pack() +
            (left_elbow or Pose()).pack() +
            (right_elbow or Pose()).pack() +
            (left_shoulder or Pose()).pack() +
            (right_shoulder or Pose()).pack()
        )
        self._send(MSG_TYPE_BODY_POSITION, data)

    def get_frame(self) -> Frame:
        """Receive a frame from the driver (blocking)."""
        header = self._recv_exact(MSG_HEADER_SIZE)
        msg_type, msg_size = struct.unpack("<II", header)

        if msg_type != MSG_TYPE_FRAME:
            raise ValueError(f"Expected frame message, got type {msg_type}")

        frame_info = self._recv_exact(FRAME_INFO_SIZE)
        width, height, eye = struct.unpack("<III", frame_info)

        pixel_size = msg_size - FRAME_INFO_SIZE
        pixel_data = self._recv_exact(pixel_size)

        return Frame(width=width, height=height, eye=eye, data=pixel_data)
