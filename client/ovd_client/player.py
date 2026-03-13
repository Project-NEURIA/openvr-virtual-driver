import math
import os
import time
from typing import Optional

import numpy as np
import pygame

from .client import Client, Pose
from .vmd import VMDPlayer


def _euler_to_quaternion(yaw: float, pitch: float) -> tuple[float, float, float, float]:
    """Convert yaw (around Y) and pitch (around X) to quaternion."""
    cy = math.cos(yaw * 0.5)
    sy = math.sin(yaw * 0.5)
    cp = math.cos(pitch * 0.5)
    sp = math.sin(pitch * 0.5)
    return (cp * cy, sp * cy, cp * sy, -sp * sy)


class Player:
    """Interactive first-person VR player with mouse/keyboard controls.

    Args:
        client: Connected Client instance
    """

    def __init__(self, client: Client) -> None:
        self._client = client

    def play(
        self,
        vmd_path: Optional[str] = None,
        audio_path: Optional[str] = None,
        sensitivity: float = 0.002,
        move_speed: float = 0.05,
    ) -> None:
        """Interactive first-person VR view with mouse/keyboard controls.

        Args:
            vmd_path: Path to VMD animation file (optional)
            audio_path: Path to audio file to sync with VMD (optional)
            sensitivity: Mouse sensitivity
            move_speed: WASD movement speed

        Controls:
        - Mouse: Look around
        - WASD: Move
        - 1: Trigger, 2: Grip, 3: A, 4: B, 5: Joystick click, 6: Menu
        - Backtick + mouse: Aim right controller
        - P: Play/Pause VMD, R: Reset VMD
        - ESC: Quit
        """
        if not self._client._socket:
            raise ConnectionError("Not connected")

        pygame.init()
        screen = pygame.display.set_mode((960, 540), pygame.RESIZABLE)
        pygame.display.set_caption("VR View")
        pygame.mouse.set_relative_mode(True)

        # Load VMD if provided
        vmd_player: Optional[VMDPlayer] = None
        if vmd_path and os.path.exists(vmd_path):
            try:
                vmd_player = VMDPlayer(vmd_path, fps=30.0)
                print(f"VMD loaded: {vmd_path}")
            except Exception as e:
                print(f"Failed to load VMD: {e}")

        # Load audio if provided
        audio_loaded = False
        if audio_path and os.path.exists(audio_path):
            try:
                pygame.mixer.init()
                pygame.mixer.music.load(audio_path)
                audio_loaded = True
                print(f"Audio loaded: {audio_path}")
            except Exception as e:
                print(f"Failed to load audio: {e}")

        print("Mouse captured. Move mouse to look around. WASD to move.")
        print("R: 1=Trigger, 2=Grip, 3=A, 4=B, 9=JoyClick | L: 5=B, 6=A, 7=Trigger, 8=Grip, 0=JoyClick")
        print("` + mouse = aim right hand. Shift + mouse = rotate left arm. Tab + mouse = aim left hand.")
        print("F = toggle ready pose (hands in front). ESC to quit.")
        if vmd_player:
            print("P = Play/Pause VMD, R = Reset. T-pose sent by default.")

        pos_x, pos_y, pos_z = 0.0, 1.7, 0.0
        yaw, pitch = 0.0, 0.0
        right_yaw, right_pitch = 0.0, 0.0
        left_yaw, left_pitch = 0.0, 0.0
        left_hand_yaw, left_hand_pitch = 0.0, 0.0
        ready_pose = False

        # Send initial T-pose
        self._send_tpose(pos_x, pos_y, pos_z, yaw, pitch)

        last_time = time.time()

        running = True
        try:
            with self._client.frame_stream() as frames:
                for frame in frames:
                    if not running:
                        break

                    current_time = time.time()
                    delta_time = current_time - last_time
                    last_time = current_time

                    position_changed = False

                    for event in pygame.event.get():
                        if event.type == pygame.QUIT:
                            running = False
                        elif event.type == pygame.KEYDOWN:
                            if event.key == pygame.K_ESCAPE:
                                running = False
                            elif event.key == pygame.K_f:
                                ready_pose = not ready_pose
                                print(f"Pose: {'Ready' if ready_pose else 'T-pose'}")
                                position_changed = True
                            elif event.key == pygame.K_p and vmd_player:
                                playing = vmd_player.toggle()
                                if audio_loaded:
                                    if playing:
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
                            dx, dy = event.rel
                            if dx != 0 or dy != 0:
                                keys_now = pygame.key.get_pressed()
                                if keys_now[pygame.K_BACKQUOTE]:
                                    right_yaw -= dx * sensitivity
                                    right_pitch -= dy * sensitivity
                                    right_pitch = max(-math.pi / 2, min(math.pi / 2, right_pitch))
                                    position_changed = True
                                elif keys_now[pygame.K_LSHIFT] or keys_now[pygame.K_RSHIFT]:
                                    left_yaw -= dx * sensitivity
                                    left_pitch -= dy * sensitivity
                                    left_pitch = max(-math.pi / 2, min(math.pi / 2, left_pitch))
                                    position_changed = True
                                elif keys_now[pygame.K_TAB]:
                                    left_hand_yaw -= dx * sensitivity
                                    left_hand_pitch -= dy * sensitivity
                                    left_hand_pitch = max(-math.pi / 2, min(math.pi / 2, left_hand_pitch))
                                    position_changed = True
                                else:
                                    yaw -= dx * sensitivity
                                    pitch -= dy * sensitivity
                                    pitch = max(-math.pi / 2 + 0.01, min(math.pi / 2 - 0.01, pitch))
                                    position_changed = True

                    # WASD movement
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
                        length = math.sqrt(move_x * move_x + move_z * move_z)
                        move_x /= length
                        move_z /= length
                        cos_yaw = math.cos(yaw)
                        sin_yaw = math.sin(yaw)
                        world_x = move_x * cos_yaw - move_z * sin_yaw
                        world_z = move_x * sin_yaw + move_z * cos_yaw
                        pos_x += world_x * move_speed
                        pos_z -= world_z * move_speed
                        position_changed = True

                    # Controller inputs
                    # Right hand: 1=trigger, 2=grip, 3=A(jump), 4=B, 9=joystick click
                    r_trigger = 1.0 if keys[pygame.K_1] else 0.0
                    r_grip = 1.0 if keys[pygame.K_2] else 0.0
                    r_a = keys[pygame.K_3]
                    r_b = keys[pygame.K_4]
                    r_joy = keys[pygame.K_9]
                    self._client.update_controller(
                        hand=1,
                        trigger=r_trigger, trigger_click=(r_trigger > 0.9), trigger_touch=(r_trigger > 0.0),
                        grip=r_grip, grip_click=(r_grip > 0.9), grip_touch=(r_grip > 0.0),
                        a_click=r_a, a_touch=r_a,
                        b_click=r_b, b_touch=r_b,
                        joystick_click=r_joy,
                    )
                    # Left hand: 5=B(menu), 6=A, 7=trigger, 8=grip, 0=joystick click
                    l_b = keys[pygame.K_5]
                    l_a = keys[pygame.K_6]
                    l_trigger = 1.0 if keys[pygame.K_7] else 0.0
                    l_grip = 1.0 if keys[pygame.K_8] else 0.0
                    l_joy = keys[pygame.K_0]
                    self._client.update_controller(
                        hand=0,
                        trigger=l_trigger, trigger_click=(l_trigger > 0.9), trigger_touch=(l_trigger > 0.0),
                        grip=l_grip, grip_click=(l_grip > 0.9), grip_touch=(l_grip > 0.0),
                        a_click=l_a, a_touch=l_a,
                        b_click=l_b, b_touch=l_b,
                        joystick_click=l_joy,
                    )

                    # Send body pose: VMD or T-pose
                    if vmd_player and (vmd_player.current_frame > 0 or vmd_player.playing):
                        if vmd_player.playing:
                            frames_to_advance = delta_time * vmd_player.fps
                            vmd_player.advance_frame(frames_to_advance)

                        hx, hy, hz, hw, hqx, hqy, hqz = vmd_player.get_head_transform(base_position=(pos_x, 0.0, pos_z))
                        body_pos = vmd_player.get_body_pose(base_position=(pos_x, 0.0, pos_z))

                        self._client.update_pose(
                            head=Pose(pos_x=hx, pos_y=hy, pos_z=hz, rot_w=hw, rot_x=hqx, rot_y=hqy, rot_z=hqz),
                            left_hand=self._tuple_to_pose(body_pos.get('left_hand')),
                            right_hand=self._tuple_to_pose(body_pos.get('right_hand')),
                            waist=self._tuple_to_pose(body_pos.get('waist')),
                            chest=self._tuple_to_pose(body_pos.get('chest')),
                            left_foot=self._tuple_to_pose(body_pos.get('left_foot')),
                            right_foot=self._tuple_to_pose(body_pos.get('right_foot')),
                            left_knee=self._tuple_to_pose(body_pos.get('left_knee')),
                            right_knee=self._tuple_to_pose(body_pos.get('right_knee')),
                            left_elbow=self._tuple_to_pose(body_pos.get('left_elbow')),
                            right_elbow=self._tuple_to_pose(body_pos.get('right_elbow')),
                            left_shoulder=self._tuple_to_pose(body_pos.get('left_shoulder')),
                            right_shoulder=self._tuple_to_pose(body_pos.get('right_shoulder')),
                        )
                    elif position_changed:
                        if ready_pose:
                            self._send_ready_pose(pos_x, pos_y, pos_z, yaw, pitch, right_yaw, right_pitch, left_yaw, left_pitch, left_hand_yaw, left_hand_pitch)
                        else:
                            self._send_tpose(pos_x, pos_y, pos_z, yaw, pitch, right_yaw, right_pitch, left_yaw, left_pitch, left_hand_yaw, left_hand_pitch)

                    # Display frame
                    if frame.eye == 0:  # Left eye only
                        frame_arr = np.frombuffer(frame.data, dtype=np.uint8).reshape((frame.height, frame.width, 4))
                        rgb_frame = frame_arr[:, :, [2, 1, 0]]  # BGRA to RGB
                        surface = pygame.surfarray.make_surface(rgb_frame.swapaxes(0, 1))
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

    def _tuple_to_pose(self, t: Optional[tuple]) -> Optional[Pose]:
        """Convert a (x, y, z, w, qx, qy, qz) tuple to Pose."""
        if t is None:
            return None
        return Pose(pos_x=t[0], pos_y=t[1], pos_z=t[2], rot_w=t[3], rot_x=t[4], rot_y=t[5], rot_z=t[6])

    def _left_arm_poses(self, pos_x: float, pos_z: float, body_yaw: float,
                        arm_yaw: float, arm_pitch: float,
                        hand_yaw: float = 0.0, hand_pitch: float = 0.0) -> tuple[Pose, Pose, Pose]:
        """Compute left shoulder, elbow, hand poses with arm rotated by aim angles.

        The shoulder is the pivot. Elbow and hand offsets are rotated around it.
        T-pose offsets from shoulder: elbow (-0.30, 0, 0), hand (-0.52, 0, 0).
        Hand rotation is independent (controlled by hand_yaw/hand_pitch).
        """
        cos_by = math.cos(-body_yaw)
        sin_by = math.sin(-body_yaw)

        # Shoulder world position (body-yaw rotated)
        sh_x = pos_x + (-0.15) * cos_by
        sh_z = pos_z + (-0.15) * sin_by

        # Arm direction from aim angles (pointing along -X in local space)
        cp = math.cos(arm_pitch)
        sp = math.sin(arm_pitch)
        cy = math.cos(arm_yaw)
        sy = math.sin(arm_yaw)
        dx = -cp * cy
        dy = sp
        dz = -cp * sy
        # Rotate arm direction by body yaw
        world_dx = dx * cos_by - dz * sin_by
        world_dz = dx * sin_by + dz * cos_by

        arm_qw, arm_qx, arm_qy, arm_qz = _euler_to_quaternion(arm_yaw + body_yaw, arm_pitch)
        hand_qw, hand_qx, hand_qy, hand_qz = _euler_to_quaternion(hand_yaw, hand_pitch)

        shoulder = Pose(pos_x=sh_x, pos_y=1.41, pos_z=sh_z,
                        rot_w=arm_qw, rot_x=arm_qx, rot_y=arm_qy, rot_z=arm_qz)
        elbow = Pose(pos_x=sh_x + 0.30 * world_dx, pos_y=1.41 + 0.30 * dy, pos_z=sh_z + 0.30 * world_dz,
                     rot_w=arm_qw, rot_x=arm_qx, rot_y=arm_qy, rot_z=arm_qz)
        hand = Pose(pos_x=sh_x + 0.52 * world_dx, pos_y=1.41 + 0.52 * dy, pos_z=sh_z + 0.52 * world_dz,
                    rot_w=hand_qw, rot_x=hand_qx, rot_y=hand_qy, rot_z=hand_qz)
        return shoulder, elbow, hand

    def _send_left_arm(self, pos_x: float, pos_z: float, body_yaw: float,
                       arm_yaw: float, arm_pitch: float,
                       hand_yaw: float = 0.0, hand_pitch: float = 0.0) -> None:
        """Send only left arm poses (shoulder as pivot)."""
        shoulder, elbow, hand = self._left_arm_poses(pos_x, pos_z, body_yaw, arm_yaw, arm_pitch, hand_yaw, hand_pitch)
        self._client.update_pose(left_shoulder=shoulder, left_elbow=elbow, left_hand=hand)

    def _send_ready_pose(self, pos_x: float, pos_y: float, pos_z: float, yaw: float, pitch: float,
                         right_yaw: float = 0.0, right_pitch: float = 0.0,
                         left_yaw: float = 0.0, left_pitch: float = 0.0,
                         left_hand_yaw: float = 0.0, left_hand_pitch: float = 0.0) -> None:
        """Send ready pose — arms bent, hands in front of chest, close together."""
        cos_yaw = math.cos(-yaw)
        sin_yaw = math.sin(-yaw)

        def rotated_pos(offset_x: float, height: float, offset_z: float = 0.0):
            rx = offset_x * cos_yaw - offset_z * sin_yaw
            rz = offset_x * sin_yaw + offset_z * cos_yaw
            return pos_x + rx, height, pos_z + rz

        def rotated_pose(offset_x: float, height: float, offset_z: float = 0.0) -> Pose:
            x, y, z = rotated_pos(offset_x, height, offset_z)
            qw, qx, qy, qz = _euler_to_quaternion(yaw, 0.0)
            return Pose(pos_x=x, pos_y=y, pos_z=z, rot_w=qw, rot_x=qx, rot_y=qy, rot_z=qz)

        head_qw, head_qx, head_qy, head_qz = _euler_to_quaternion(yaw, pitch)
        head = Pose(pos_x=pos_x, pos_y=pos_y, pos_z=pos_z, rot_w=head_qw, rot_x=head_qx, rot_y=head_qy, rot_z=head_qz)

        # Right hand in front of chest
        r_qw, r_qx, r_qy, r_qz = _euler_to_quaternion(yaw + right_yaw, right_pitch)
        rh_x, rh_y, rh_z = rotated_pos(0.15, 1.45, -0.25)
        right_hand = Pose(pos_x=rh_x, pos_y=rh_y, pos_z=rh_z,
                          rot_w=r_qw, rot_x=r_qx, rot_y=r_qy, rot_z=r_qz)

        # Left arm uses shoulder pivot — offset so default is in front of chest
        # In T-pose arm points along -X (yaw=0). Rotate ~90° to point forward (-Z).
        # Also pitch up slightly so hand is at chest height.
        ready_arm_yaw = left_yaw + math.pi / 2
        ready_arm_pitch = left_pitch - 0.3
        left_shoulder, left_elbow, left_hand = self._left_arm_poses(pos_x, pos_z, yaw, ready_arm_yaw, ready_arm_pitch, left_hand_yaw, left_hand_pitch)

        # Right elbow bent forward
        re_x, re_y, re_z = rotated_pos(0.20, 1.35, -0.10)
        body_qw, body_qx, body_qy, body_qz = _euler_to_quaternion(yaw, 0.0)

        self._client.update_pose(
            head=head,
            waist=rotated_pose(0.0, 0.93),
            chest=rotated_pose(0.0, 1.29),
            left_shoulder=left_shoulder,
            right_shoulder=rotated_pose(0.15, 1.41),
            left_elbow=left_elbow,
            right_elbow=Pose(pos_x=re_x, pos_y=re_y, pos_z=re_z, rot_w=body_qw, rot_x=body_qx, rot_y=body_qy, rot_z=body_qz),
            left_hand=left_hand,
            right_hand=right_hand,
            left_knee=rotated_pose(-0.09, 0.46),
            right_knee=rotated_pose(0.09, 0.46),
            left_foot=rotated_pose(-0.09, 0.06),
            right_foot=rotated_pose(0.09, 0.06),
        )

    def _send_tpose(self, pos_x: float, pos_y: float, pos_z: float, yaw: float, pitch: float,
                    right_yaw: float = 0.0, right_pitch: float = 0.0,
                    left_yaw: float = 0.0, left_pitch: float = 0.0,
                    left_hand_yaw: float = 0.0, left_hand_pitch: float = 0.0) -> None:
        """Send T-pose body position rotated by yaw, with optional hand aim."""
        cos_yaw = math.cos(-yaw)
        sin_yaw = math.sin(-yaw)

        def rotated_pose(offset_x: float, height: float, offset_z: float = 0.0) -> Pose:
            rx = offset_x * cos_yaw - offset_z * sin_yaw
            rz = offset_x * sin_yaw + offset_z * cos_yaw
            qw, qx, qy, qz = _euler_to_quaternion(yaw, 0.0)
            return Pose(pos_x=pos_x + rx, pos_y=height, pos_z=pos_z + rz, rot_w=qw, rot_x=qx, rot_y=qy, rot_z=qz)

        head_qw, head_qx, head_qy, head_qz = _euler_to_quaternion(yaw, pitch)
        head = Pose(pos_x=pos_x, pos_y=pos_y, pos_z=pos_z, rot_w=head_qw, rot_x=head_qx, rot_y=head_qy, rot_z=head_qz)

        # Right hand aim
        r_qw, r_qx, r_qy, r_qz = _euler_to_quaternion(yaw + right_yaw, right_pitch)
        right_hand = Pose(pos_x=pos_x + 0.67 * cos_yaw, pos_y=1.41, pos_z=pos_z + 0.67 * sin_yaw,
                          rot_w=r_qw, rot_x=r_qx, rot_y=r_qy, rot_z=r_qz)

        # Left arm (shoulder as pivot, hand rotation independent)
        left_shoulder, left_elbow, left_hand = self._left_arm_poses(pos_x, pos_z, yaw, left_yaw, left_pitch, left_hand_yaw, left_hand_pitch)

        self._client.update_pose(
            head=head,
            waist=rotated_pose(0.0, 0.93),
            chest=rotated_pose(0.0, 1.29),
            left_shoulder=left_shoulder,
            right_shoulder=rotated_pose(0.15, 1.41),
            left_elbow=left_elbow,
            right_elbow=rotated_pose(0.45, 1.41),
            left_hand=left_hand,
            right_hand=right_hand,
            left_knee=rotated_pose(-0.09, 0.46),
            right_knee=rotated_pose(0.09, 0.46),
            left_foot=rotated_pose(-0.09, 0.06),
            right_foot=rotated_pose(0.09, 0.06),
        )
