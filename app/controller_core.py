#!/usr/bin/env python3
"""
controller_core.py – zentrale Logik für Joystick/Keyboard und Serial I/O
"""
import time
import logging
import pygame
import config
import serial_manager

class ControllerCore:
    """
    Core logic for joystick and keyboard input, serial reconnect/drain,
    sending commands based on mappings.
    """
    def __init__(self):
        logging.info("[info] Initializing ControllerCore...")
        pygame.init()
        pygame.joystick.init()
        self._gyro = None   # placeholder for last‐read gyro tuple
        # Liste aller verbundenen Joysticks
        self.joysticks = []
        self._init_joysticks()

        # Button-Debounce-/Repeat-State
        self.btn_last = {btn: 0.0 for btn in config.JOYSTICK_BUTTON_MAPPING}
        self.btn_repeat = {btn: False for btn in config.JOYSTICK_BUTTON_MAPPING}

        # Zeit für nächstes Axis-Kommando
        self.next_axis_time = 0.0

        logging.info("[info] ControllerCore initialized.")

    def _init_joysticks(self):
        """Initial scan: alle aktuell eingesteckten Joysticks verbinden"""
        count = pygame.joystick.get_count()
        for idx in range(count):
            try:
                joy = pygame.joystick.Joystick(idx)
                joy.init()
                self.joysticks.append(joy)
                jid = getattr(joy, 'get_instance_id', joy.get_id)()
                logging.info(f"[info] Joystick '{joy.get_name()}' connected (instance {jid})")
            except Exception as e:
                logging.error(f"[error] Failed to init joystick {idx}: {e}")

    def step(self):
        now = time.time()

        # Serielle Verbindung: reconnect & drain
        serial_manager.try_reconnect()
        serial_manager.drain()

        # Pygame Event-Handling
        try:
            pygame.event.pump()
            events = pygame.event.get()
        except Exception as e:
            logging.error(f"Pygame event pump/get failed: {e}")
            events = []

        for event in events:
            if event.type == pygame.JOYDEVICEADDED:
                idx = event.device_index
                try:
                    joy = pygame.joystick.Joystick(idx)
                    joy.init()
                    self.joysticks.append(joy)
                    jid = getattr(joy, 'get_instance_id', joy.get_id)()
                    logging.info(f"[info] Joystick '{joy.get_name()}' connected (instance {jid})")
                except Exception as e:
                    logging.error(f"[error] Could not add joystick at index {idx}: {e}")
            elif event.type == pygame.JOYDEVICEREMOVED:
                inst = getattr(event, 'instance_id', None)
                for joy in list(self.joysticks):
                    jid = getattr(joy, 'get_instance_id', joy.get_id)()
                    if jid == inst:
                        logging.info(f"[info] Joystick '{joy.get_name()}' disconnected (instance {jid})")
                        self.joysticks.remove(joy)
                        break

        try: 
            for joy in self.joysticks:
                for btn, cmd in config.JOYSTICK_BUTTON_MAPPING.items():
                    try:
                        pressed = joy.get_button(btn)
                    except Exception:
                        continue
                    if pressed:
                        delay = config.REPEAT_DELAY if self.btn_repeat[btn] else config.DEBOUNCE_MS
                        if now - self.btn_last[btn] >= delay:
                            serial_manager.send(cmd)
                            self.btn_last[btn] = now
                            self.btn_repeat[btn] = True
                    else:
                        self.btn_repeat[btn] = False

            # Polling der Achsen für kontinuierliche Befehle
            for joy in self.joysticks:
                for cmd, axis in config.AXIS_MAPPING.items():
                    try:
                        val = joy.get_axis(axis)
                    except Exception:
                        continue
                    if abs(val) > config.STICK_DEADZONE and now >= self.next_axis_time:
                        delta = val * (config.STICK_SENSE / config.UPDATES_PER_SEC)
                        serial_manager.send(f"{cmd} {delta:+.3f}")
                        self.next_axis_time = now + 1.0 / config.UPDATES_PER_SEC

            for cmd, axis_list in config.TRIGGER_MAPPING.items():
                for axis, sign in axis_list:
                    val = joy.get_axis(axis)
                    if val > config.TRIGGER_DEADZONE/2 and now >= self.next_axis_time:
                        delta = sign * val * (config.TRIGGER_SENSE / config.UPDATES_PER_SEC)
                        serial_manager.send(f"{cmd} {delta:+.3f}")
                        self.next_axis_time = now + 1.0 / config.UPDATES_PER_SEC 

        except: 
            pass

                # ─── if the UI asked for gyro, sample & send it ─────────────────
        if self.joysticks:
            try:
                self._gyro = None
            except:
                pass

    def get_gyro(self):
        """
        Return the latest gyro reading as (x,y,z), or None if unavailable.
        Implement gyro sampling in your main loop to populate self._gyro.
        """
        return self._gyro
    
    def shutdown(self):
        logging.info("[info] Shutting down ControllerCore...")
        serial_manager.close_serial()
        pygame.quit()