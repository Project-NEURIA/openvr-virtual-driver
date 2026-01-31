"""VMD parser with forward kinematics."""
import struct
import math
from dataclasses import dataclass


@dataclass
class BoneKeyframe:
    bone_name: str
    frame_number: int
    position: tuple[float, float, float]
    rotation: tuple[float, float, float, float]  # x, y, z, w


def read_fixed_string(f, length: int) -> str:
    data = f.read(length)
    null_pos = data.find(b'\x00')
    if null_pos != -1:
        data = data[:null_pos]
    try:
        return data.decode('shift-jis')
    except:
        return data.decode('utf-8', errors='replace')


def parse_vmd(filepath: str):
    with open(filepath, 'rb') as f:
        f.read(30)  # signature
        model_name = read_fixed_string(f, 20)
        bone_count = struct.unpack('<I', f.read(4))[0]

        keyframes = []
        max_frame = 0

        for _ in range(bone_count):
            bone_name = read_fixed_string(f, 15)
            frame = struct.unpack('<I', f.read(4))[0]
            max_frame = max(max_frame, frame)
            px, py, pz = struct.unpack('<3f', f.read(12))
            rx, ry, rz, rw = struct.unpack('<4f', f.read(16))
            f.read(64)  # interpolation

            keyframes.append(BoneKeyframe(bone_name, frame, (px, py, pz), (rx, ry, rz, rw)))

        return model_name, keyframes, max_frame


# Quaternion math
def quat_multiply(q1, q2):
    """Multiply quaternions (w, x, y, z)."""
    w1, x1, y1, z1 = q1
    w2, x2, y2, z2 = q2
    return (
        w1*w2 - x1*x2 - y1*y2 - z1*z2,
        w1*x2 + x1*w2 + y1*z2 - z1*y2,
        w1*y2 - x1*z2 + y1*w2 + z1*x2,
        w1*z2 + x1*y2 - y1*x2 + z1*w2
    )


def quat_rotate_vec(q, v):
    """Rotate vector v by quaternion q (w, x, y, z)."""
    w, x, y, z = q
    vx, vy, vz = v

    # q * v * q^-1
    # Optimized version
    tx = 2 * (y * vz - z * vy)
    ty = 2 * (z * vx - x * vz)
    tz = 2 * (x * vy - y * vx)

    return (
        vx + w * tx + y * tz - z * ty,
        vy + w * ty + z * tx - x * tz,
        vz + w * tz + x * ty - y * tx
    )


def quat_identity():
    return (1.0, 0.0, 0.0, 0.0)


class VMDPlayer:
    """VMD player with forward kinematics."""

    # T-pose skeleton: bone -> (parent, offset_from_parent in meters)
    # Offset is in parent's local space
    # Hip at origin, Y-up, Z-forward
    SKELETON = {
        # Root
        'センター': (None, (0.0, 0.0, 0.0)),

        # Spine (goes up from hip)
        '上半身': ('センター', (0.0, 0.18, 0.0)),    # Lower spine
        '上半身2': ('上半身', (0.0, 0.18, 0.0)),     # Upper spine (chest)

        # Neck and head
        '首': ('上半身2', (0.0, 0.12, 0.0)),         # Neck
        '頭': ('首', (0.0, 0.12, 0.0)),              # Head (eye level)

        # Lower body (same as center, controls legs)
        '下半身': ('センター', (0.0, 0.0, 0.0)),

        # Left arm chain
        '左肩': ('上半身2', (-0.05, 0.12, 0.0)),     # Left shoulder
        '左腕': ('左肩', (-0.10, 0.0, 0.0)),         # Left upper arm (shorter)
        '左ひじ': ('左腕', (-0.20, 0.0, 0.0)),       # Left elbow (shorter upper arm)
        '左手首': ('左ひじ', (-0.22, 0.0, 0.0)),     # Left wrist

        # Right arm chain
        '右肩': ('上半身2', (0.05, 0.12, 0.0)),      # Right shoulder
        '右腕': ('右肩', (0.10, 0.0, 0.0)),          # Right upper arm (shorter)
        '右ひじ': ('右腕', (0.20, 0.0, 0.0)),        # Right elbow (shorter upper arm)
        '右手首': ('右ひじ', (0.22, 0.0, 0.0)),      # Right wrist

        # Left leg chain
        '左足': ('下半身', (-0.09, -0.05, 0.0)),     # Left thigh
        '左ひざ': ('左足', (0.0, -0.42, 0.0)),       # Left knee
        '左足首': ('左ひざ', (0.0, -0.40, 0.0)),     # Left ankle

        # Right leg chain
        '右足': ('下半身', (0.09, -0.05, 0.0)),      # Right thigh
        '右ひざ': ('右足', (0.0, -0.42, 0.0)),       # Right knee
        '右足首': ('右ひざ', (0.0, -0.40, 0.0)),     # Right ankle
    }

    # Map bones to OpenVR trackers
    BONE_TO_TRACKER = {
        '左手首': 'left_hand',
        '右手首': 'right_hand',
        'センター': 'waist',
        '上半身2': 'chest',
        '左足首': 'left_foot',
        '右足首': 'right_foot',
        '左ひざ': 'left_knee',
        '右ひざ': 'right_knee',
        '左ひじ': 'left_elbow',
        '右ひじ': 'right_elbow',
        '左肩': 'left_shoulder',
        '右肩': 'right_shoulder',
    }

    def __init__(self, vmd_path: str, fps: float = 30.0):
        model_name, keyframes, max_frame = parse_vmd(vmd_path)
        self.fps = fps
        self.total_frames = max_frame
        self.current_frame = 0.0
        self.playing = False
        self.loop = True

        # Index keyframes by bone
        self._keyframes = {}
        for kf in keyframes:
            if kf.bone_name not in self._keyframes:
                self._keyframes[kf.bone_name] = []
            self._keyframes[kf.bone_name].append(kf)

        # Sort by frame
        for bone in self._keyframes:
            self._keyframes[bone].sort(key=lambda x: x.frame_number)

        # HMD at 1.7m means roughly 1.8m tall person
        # Hip is at about 52% of height = 0.93m
        self.hip_height = 0.93

        # MMD to meters scale (センター position is in MMD units)
        self.position_scale = 0.08

        print(f"VMD: {model_name}, {max_frame} frames, hip at {self.hip_height}m")

    def _get_rotation(self, bone_name: str, frame: int) -> tuple:
        """Get bone rotation at frame (returns w, x, y, z quaternion)."""
        if bone_name not in self._keyframes:
            return quat_identity()

        kfs = self._keyframes[bone_name]

        # Find keyframe at or before this frame
        result = None
        for kf in kfs:
            if kf.frame_number <= frame:
                result = kf
            else:
                break

        if result is None:
            return quat_identity()

        # VMD stores (x, y, z, w), convert to (w, x, y, z)
        # Y and Z negated
        rx, ry, rz, rw = result.rotation
        return (rw, rx, -ry, -rz)

    def _get_center_position(self, frame: int) -> tuple:
        """Get center bone position offset in meters."""
        if 'センター' not in self._keyframes:
            return (0.0, 0.0, 0.0)

        kfs = self._keyframes['センター']
        result = None
        for kf in kfs:
            if kf.frame_number <= frame:
                result = kf
            else:
                break

        if result is None:
            return (0.0, 0.0, 0.0)

        # Convert to meters, flip Z for OpenVR
        px, py, pz = result.position
        return (px * self.position_scale, py * self.position_scale, -pz * self.position_scale)

    def _compute_bone_world_transforms(self, frame: int) -> dict:
        """Compute world position and rotation for all bones using forward kinematics."""
        # World transform: (position, rotation) where rotation is (w, x, y, z)
        world_transforms = {}

        # Process bones in order (parents before children)
        bone_order = [
            'センター', '上半身', '上半身2', '首', '頭', '下半身',
            '左肩', '左腕', '左ひじ', '左手首',
            '右肩', '右腕', '右ひじ', '右手首',
            '左足', '左ひざ', '左足首',
            '右足', '右ひざ', '右足首',
        ]

        # Get center position offset (root motion)
        center_offset = self._get_center_position(frame)

        for bone_name in bone_order:
            if bone_name not in self.SKELETON:
                continue

            parent_name, local_offset = self.SKELETON[bone_name]
            local_rotation = self._get_rotation(bone_name, frame)

            if parent_name is None:
                # Root bone (センター)
                world_pos = (center_offset[0], self.hip_height + center_offset[1], center_offset[2])
                world_rot = local_rotation
            else:
                # Get parent world transform
                parent_pos, parent_rot = world_transforms.get(parent_name, ((0, 0, 0), quat_identity()))

                # Rotate local offset by parent rotation
                rotated_offset = quat_rotate_vec(parent_rot, local_offset)

                # World position = parent position + rotated offset
                world_pos = (
                    parent_pos[0] + rotated_offset[0],
                    parent_pos[1] + rotated_offset[1],
                    parent_pos[2] + rotated_offset[2]
                )

                # World rotation = parent rotation * local rotation
                world_rot = quat_multiply(parent_rot, local_rotation)

            world_transforms[bone_name] = (world_pos, world_rot)

        return world_transforms

    def get_head_transform(self, base_position: tuple = (0.0, 0.0, 0.0)) -> tuple:
        """Get head position and rotation for HMD.

        Returns (x, y, z, qw, qx, qy, qz)
        """
        frame = int(self.current_frame)
        transforms = self._compute_bone_world_transforms(frame)

        if '頭' in transforms:
            pos, rot = transforms['頭']
            return (
                pos[0] + base_position[0],
                pos[1] + base_position[1],
                pos[2] + base_position[2],
                rot[0], rot[1], rot[2], rot[3]
            )

        # Default head position if not found
        return (base_position[0], base_position[1] + 1.7, base_position[2], 1.0, 0.0, 0.0, 0.0)

    def _compute_knee_from_ik(self, hip_pos: tuple, foot_pos: tuple, is_left: bool) -> tuple:
        """Compute knee position using simple IK (knee bends forward)."""
        # Thigh and shin lengths
        thigh_len = 0.42
        shin_len = 0.40

        # Vector from hip to foot
        dx = foot_pos[0] - hip_pos[0]
        dy = foot_pos[1] - hip_pos[1]
        dz = foot_pos[2] - hip_pos[2]
        dist = (dx*dx + dy*dy + dz*dz) ** 0.5

        if dist < 0.01:
            dist = 0.01

        # If leg is fully extended or too short, just put knee in middle
        total_len = thigh_len + shin_len
        if dist >= total_len:
            # Fully extended
            t = thigh_len / total_len
            return (
                hip_pos[0] + dx * t,
                hip_pos[1] + dy * t,
                hip_pos[2] + dz * t
            )

        # Use law of cosines to find knee angle
        # cos(angle) = (a² + b² - c²) / (2ab)
        cos_angle = (thigh_len*thigh_len + dist*dist - shin_len*shin_len) / (2 * thigh_len * dist)
        cos_angle = max(-1, min(1, cos_angle))

        # Distance along hip-foot line to knee projection
        proj_dist = thigh_len * cos_angle

        # Perpendicular distance (knee sticks out)
        perp_dist = thigh_len * (1 - cos_angle*cos_angle) ** 0.5

        # Normalize hip-to-foot direction
        nx, ny, nz = dx/dist, dy/dist, dz/dist

        # Point along the line
        mid_x = hip_pos[0] + nx * proj_dist
        mid_y = hip_pos[1] + ny * proj_dist
        mid_z = hip_pos[2] + nz * proj_dist

        # Knee bends forward (positive Z in character space, but we need to account for orientation)
        # Simple approach: knee goes forward (negative Z in OpenVR) and slightly outward
        knee_x = mid_x
        knee_y = mid_y
        knee_z = mid_z - perp_dist  # Forward bend

        return (knee_x, knee_y, knee_z)

    def get_body_pose(self, base_position: tuple = (0.0, 0.0, 0.0)) -> dict:
        """Get body pose for all trackers at current frame."""
        frame = int(self.current_frame)
        transforms = self._compute_bone_world_transforms(frame)

        body_pose = {}
        for bone_name, tracker_name in self.BONE_TO_TRACKER.items():
            if bone_name in transforms:
                pos, rot = transforms[bone_name]
                # Add base position offset
                body_pose[tracker_name] = (
                    pos[0] + base_position[0],
                    pos[1] + base_position[1],
                    pos[2] + base_position[2],
                    rot[0], rot[1], rot[2], rot[3]  # w, x, y, z
                )

        # Compute knee positions from IK (hip and foot positions)
        if 'waist' in body_pose and 'left_foot' in body_pose:
            hip_pos = (body_pose['waist'][0], body_pose['waist'][1], body_pose['waist'][2])
            # Left hip is offset from center
            left_hip = (hip_pos[0] - 0.09, hip_pos[1] - 0.05, hip_pos[2])
            left_foot = (body_pose['left_foot'][0], body_pose['left_foot'][1], body_pose['left_foot'][2])
            knee_pos = self._compute_knee_from_ik(left_hip, left_foot, True)
            body_pose['left_knee'] = (knee_pos[0], knee_pos[1], knee_pos[2], 1.0, 0.0, 0.0, 0.0)

        if 'waist' in body_pose and 'right_foot' in body_pose:
            hip_pos = (body_pose['waist'][0], body_pose['waist'][1], body_pose['waist'][2])
            # Right hip is offset from center
            right_hip = (hip_pos[0] + 0.09, hip_pos[1] - 0.05, hip_pos[2])
            right_foot = (body_pose['right_foot'][0], body_pose['right_foot'][1], body_pose['right_foot'][2])
            knee_pos = self._compute_knee_from_ik(right_hip, right_foot, False)
            body_pose['right_knee'] = (knee_pos[0], knee_pos[1], knee_pos[2], 1.0, 0.0, 0.0, 0.0)

        # Fill missing with defaults
        defaults = {
            'left_hand': (-0.5, 1.0, 0.0),
            'right_hand': (0.5, 1.0, 0.0),
            'waist': (0.0, 0.93, 0.0),
            'chest': (0.0, 1.2, 0.0),
            'left_foot': (-0.1, 0.05, 0.0),
            'right_foot': (0.1, 0.05, 0.0),
            'left_knee': (-0.1, 0.45, 0.0),
            'right_knee': (0.1, 0.45, 0.0),
            'left_elbow': (-0.4, 1.1, 0.0),
            'right_elbow': (0.4, 1.1, 0.0),
            'left_shoulder': (-0.15, 1.35, 0.0),
            'right_shoulder': (0.15, 1.35, 0.0),
        }

        for name, pos in defaults.items():
            if name not in body_pose:
                body_pose[name] = (
                    pos[0] + base_position[0],
                    pos[1] + base_position[1],
                    pos[2] + base_position[2],
                    1.0, 0.0, 0.0, 0.0
                )

        return body_pose

    def advance_frame(self, delta: float = 1.0):
        if not self.playing:
            return
        self.current_frame += delta
        if self.current_frame >= self.total_frames:
            self.current_frame = 0 if self.loop else self.total_frames
            if not self.loop:
                self.playing = False

    def toggle(self):
        self.playing = not self.playing
        return self.playing

    def reset(self):
        self.current_frame = 0
