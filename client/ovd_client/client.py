import socket
import struct
from contextlib import contextmanager
from itertools import chain
from dataclasses import dataclass
from typing import Generator, Optional

import numpy as np
from numpy.typing import NDArray


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
    pose: Pose
    data: bytes


@dataclass
class StereoFrame:
    """A synchronized pair of left and right eye frames from the same Present() call."""
    left: Frame
    right: Frame


class Camera:
    """Computes per-eye camera intrinsics & extrinsics for the virtual HMD."""

    def __init__(
        self,
        render_width: int = 1080,
        render_height: int = 1080,
        proj_left: float = -1.0,
        proj_right: float = 1.0,
        proj_top: float = -1.0,
        proj_bottom: float = 1.0,
        ipd: float = 0.063,
    ) -> None:
        self.render_width = render_width
        self.render_height = render_height
        self.proj_left = proj_left
        self.proj_right = proj_right
        self.proj_top = proj_top
        self.proj_bottom = proj_bottom
        self.ipd = ipd

        # Pre-compute intrinsics (static for a given HMD configuration)
        fx = render_width / (proj_right - proj_left)
        fy = render_height / (proj_bottom - proj_top)
        cx = -proj_left * render_width / (proj_right - proj_left)
        cy = -proj_top * render_height / (proj_bottom - proj_top)
        self._intrinsics = np.array([
            [fx, 0.0, cx],
            [0.0, fy, cy],
            [0.0, 0.0, 1.0],
        ], dtype=np.float64)

    @property
    def intrinsics(self) -> NDArray[np.float64]:
        """3x3 camera intrinsics matrix K (static, does not change with pose)."""
        return self._intrinsics.copy()

    def extrinsics(self, pose: "Pose", eye: int = 0) -> NDArray[np.float64]:
        """4x4 world-to-camera extrinsics matrix for the given head pose and eye.

        Args:
            pose: Current head pose (position + quaternion).
            eye: 0 = left eye, 1 = right eye.

        Returns:
            4x4 extrinsics matrix T (world-to-camera transform).
        """
        # Head-to-world: rotation from quaternion + translation
        R = _quat_to_rotation_matrix(pose.rot_w, pose.rot_x, pose.rot_y, pose.rot_z)
        t = np.array([pose.pos_x, pose.pos_y, pose.pos_z])

        # Eye offset along the head's local X axis
        eye_offset_x = (-0.5 if eye == 0 else 0.5) * self.ipd
        t_eye = t + R[:, 0] * eye_offset_x

        # Build world-to-eye transform (inverse of eye-to-world)
        T = np.eye(4, dtype=np.float64)
        T[:3, :3] = R.T
        T[:3, 3] = -R.T @ t_eye
        return T


def _quat_to_rotation_matrix(w: float, x: float, y: float, z: float) -> NDArray[np.float64]:
    """Convert quaternion (w, x, y, z) to a 3x3 rotation matrix."""
    ww, xx, yy, zz = w * w, x * x, y * y, z * z
    wx, wy, wz = w * x, w * y, w * z
    xy, xz, yz = x * y, x * z, y * z
    return np.array([
        [ww + xx - yy - zz, 2 * (xy - wz),     2 * (xz + wy)],
        [2 * (xy + wz),     ww - xx + yy - zz,  2 * (yz - wx)],
        [2 * (xz - wy),     2 * (yz + wx),      ww - xx - yy + zz],
    ], dtype=np.float64)


class Client:
    """TCP client for communicating with the OpenVR virtual driver."""

    def __init__(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT) -> None:
        self.host = host
        self.port = port
        self._socket: Optional[socket.socket] = None
        self._camera = Camera()
        self._camera_calibrated = False

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

    def _calibrate_camera(self, frame: Frame) -> None:
        """Calibrate Camera from the first received frame. Called once automatically."""
        if self._camera_calibrated:
            return
        if (frame.width != self._camera.render_width
                or frame.height != self._camera.render_height):
            self._camera = Camera(
                render_width=frame.width,
                render_height=frame.height,
                proj_left=self._camera.proj_left,
                proj_right=self._camera.proj_right,
                proj_top=self._camera.proj_top,
                proj_bottom=self._camera.proj_bottom,
                ipd=self._camera.ipd,
            )
        self._camera_calibrated = True

    def get_intrinsics(self) -> NDArray[np.float64]:
        """Return the 3x3 camera intrinsics matrix K."""
        return self._camera.intrinsics

    def get_extrinsics(self, frame: Frame) -> NDArray[np.float64]:
        """Return the 4x4 world-to-camera extrinsics matrix for the given frame."""
        return self._camera.extrinsics(frame.pose, frame.eye)

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
        buf = bytearray(size)
        view = memoryview(buf)
        pos = 0
        while pos < size:
            n = self._socket.recv_into(view[pos:])
            if n == 0:
                raise ConnectionError("Connection closed")
            pos += n
        return bytes(buf)

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
            # Calibrate camera from first frame
            first = self.get_frame()
            self._calibrate_camera(first)
            yield chain((first,), self._frame_iter())
        finally:
            try:
                self.stop_frame_stream()
            except Exception:
                pass

    def _frame_iter(self) -> Generator[Frame, None, None]:
        """Yield frames from the driver indefinitely."""
        while True:
            yield self.get_frame()

    def _stereo_iter(self) -> Generator[StereoFrame, None, None]:
        """Yield stereo frame pairs from the driver indefinitely."""
        while True:
            yield self.get_stereo_frame()

    def get_stereo_frame(self) -> StereoFrame:
        """Receive a synchronized left+right frame pair (blocking).

        Reads frames until a left (eye=0) followed by right (eye=1) pair is
        found, discarding any orphaned frames to ensure correct pairing.
        """
        # Find a left eye frame
        left = self.get_frame()
        while left.eye != 0:
            left = self.get_frame()

        # The next frame should be the right eye from the same Present() call
        right = self.get_frame()
        if right.eye != 1:
            # Lost sync — search again recursively
            return self.get_stereo_frame()

        return StereoFrame(left=left, right=right)

    @contextmanager
    def stereo_stream(self) -> Generator:
        """Context manager that yields synchronized stereo frame pairs.

        Usage:
            with client.stereo_stream() as frames:
                for stereo in frames:
                    process(stereo.left, stereo.right)
        """
        self.start_frame_stream()
        try:
            first = self.get_stereo_frame()
            self._calibrate_camera(first.left)
            yield chain((first,), self._stereo_iter())
        finally:
            try:
                self.stop_frame_stream()
            except Exception:
                pass

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

        pose_data = self._recv_exact(POSE_SIZE)
        pos_x, pos_y, pos_z, rot_w, rot_x, rot_y, rot_z = struct.unpack("<7f", pose_data)
        pose = Pose(pos_x=pos_x, pos_y=pos_y, pos_z=pos_z,
                    rot_w=rot_w, rot_x=rot_x, rot_y=rot_y, rot_z=rot_z)

        pixel_size = msg_size - FRAME_INFO_SIZE - POSE_SIZE
        pixel_data = self._recv_exact(pixel_size)

        return Frame(width=width, height=height, eye=eye, pose=pose, data=pixel_data)
