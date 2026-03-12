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
        print("1=Trigger, 2=Grip, 3=A, 4=B, 5=Joystick click, 6=Menu.")
        print("Hold ` (backtick) + mouse = aim right controller. ESC to quit.")
        if vmd_player:
            print("P = Play/Pause VMD, R = Reset. T-pose sent by default.")

        pos_x, pos_y, pos_z = 0.0, 1.7, 0.0
        yaw, pitch = 0.0, 0.0
        right_yaw, right_pitch = 0.0, 0.0

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
                    trigger = 1.0 if keys[pygame.K_1] else 0.0
                    grip = 1.0 if keys[pygame.K_2] else 0.0
                    a_click = keys[pygame.K_3]
                    b_click = keys[pygame.K_4]
                    joystick_click = keys[pygame.K_5]
                    menu_click = keys[pygame.K_6]

                    self._client.update_controller(
                        trigger=trigger, trigger_click=(trigger > 0.9), trigger_touch=(trigger > 0.0),
                        grip=grip, grip_click=(grip > 0.9), grip_touch=(grip > 0.0),
                        a_click=a_click, a_touch=a_click,
                        b_click=b_click, b_touch=b_click,
                        joystick_click=joystick_click,
                        menu_click=menu_click,
                        right_yaw=right_yaw, right_pitch=right_pitch,
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
                        self._send_tpose(pos_x, pos_y, pos_z, yaw, pitch)

                    # Display frame
                    if frame.eye == 0:  # Left eye only
                        frame_arr = np.frombuffer(frame.data, dtype=np.uint8).reshape((frame.height, frame.width, 4))
                        rgb_frame = frame_arr[:, :, :3]  # RGBA to RGB
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

    def _send_tpose(self, pos_x: float, pos_y: float, pos_z: float, yaw: float, pitch: float) -> None:
        """Send T-pose body position rotated by yaw."""
        cos_yaw = math.cos(yaw)
        sin_yaw = math.sin(yaw)

        def rotated_pose(offset_x: float, height: float, offset_z: float = 0.0) -> Pose:
            rx = offset_x * cos_yaw - offset_z * sin_yaw
            rz = offset_x * sin_yaw + offset_z * cos_yaw
            qw, qx, qy, qz = _euler_to_quaternion(yaw, 0.0)
            return Pose(pos_x=pos_x + rx, pos_y=height, pos_z=pos_z + rz, rot_w=qw, rot_x=qx, rot_y=qy, rot_z=qz)

        head_qw, head_qx, head_qy, head_qz = _euler_to_quaternion(yaw, pitch)
        head = Pose(pos_x=pos_x, pos_y=pos_y, pos_z=pos_z, rot_w=head_qw, rot_x=head_qx, rot_y=head_qy, rot_z=head_qz)

        self._client.update_pose(
            head=head,
            waist=rotated_pose(0.0, 0.93),
            chest=rotated_pose(0.0, 1.29),
            left_shoulder=rotated_pose(-0.15, 1.41),
            right_shoulder=rotated_pose(0.15, 1.41),
            left_elbow=rotated_pose(-0.45, 1.41),
            right_elbow=rotated_pose(0.45, 1.41),
            left_hand=rotated_pose(-0.67, 1.41),
            right_hand=rotated_pose(0.67, 1.41),
            left_knee=rotated_pose(-0.09, 0.46),
            right_knee=rotated_pose(0.09, 0.46),
            left_foot=rotated_pose(-0.09, 0.06),
            right_foot=rotated_pose(0.09, 0.06),
        )
