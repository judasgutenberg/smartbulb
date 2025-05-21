
import socket
import json
import random
import time
import threading
import ipaddress
from typing import List, Dict, Tuple, Union, Optional
from api import post_api


class WiZController:
    PORT = 38899

    COLOR_MAP = {
        "red": (255, 0, 0),
        "green": (0, 255, 0),
        "blue": (0, 0, 255),
        "yellow": (255, 255, 0),
        "purple": (128, 0, 128),
        "pink": (255, 192, 203),
        "orange": (255, 165, 0),
        "cyan": (0, 255, 255),
        "white": (255, 255, 255)
    }

    TEMP_MAP = {
        "warm white": 2700,
        "soft white": 3000,
        "neutral": 4000,
        "cool white": 5000,
        "daylight": 6500
    }

    def __init__(self, ip_list: List[str] = None, discover: bool = False, timeout: int = 5):
        self.bulbs = ip_list or []
        self.bulb_info = {}
        self.timeout = timeout
        self.rave_thread = None
        self.rave_running = False
        if discover:
            self.start()

    def start(self) -> List[str]:
        print("Scanning for WiZ bulbs on the network...")
        local_ip = self._get_local_ip()
        if not local_ip:
            print("Error: Could not determine local IP address")
            return self.bulbs

        try:
            network = ipaddress.IPv4Interface(f"{local_ip}/24").network
        except Exception as e:
            print(f"Error determining network: {e}")
            return self.bulbs

        discovery_msg = {"method": "getPilot", "params": {}}
        discovered = []

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(0.5)
            sock.bind(('', 0))
            for ip in network.hosts():
                try:
                    sock.sendto(json.dumps(discovery_msg).encode(), (str(ip), self.PORT))
                except:
                    pass
            start_time = time.time()
            while time.time() - start_time < self.timeout:
                try:
                    data, addr = sock.recvfrom(1024)
                    ip = addr[0]
                    if ip not in discovered and ip not in self.bulbs:
                        try:
                            response = json.loads(data.decode())
                            if "result" in response:
                                discovered.append(ip)
                                self.bulb_info[ip] = response["result"]
                                print(f"Discovered WiZ bulb at {ip}")
                        except json.JSONDecodeError:
                            pass
                except socket.timeout:
                    pass

        for ip in discovered:
            if ip not in self.bulbs:
                self.bulbs.append(ip)

        print(f"Discovery complete. Found {len(discovered)} new bulbs.")
        return self.bulbs

    def _get_local_ip(self) -> Optional[str]:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except:
            return None

    def send(self, ip: str, payload: dict) -> Optional[dict]:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(2.0)
                sock.sendto(json.dumps(payload).encode(), (ip, self.PORT))
                try:
                    data, _ = sock.recvfrom(1024)
                    return json.loads(data.decode())
                except:
                    return None
        except Exception as e:
            print(f"Error sending to {ip}: {e}")
            return None

    def get_status(self, ip: str) -> Optional[dict]:
        response = self.send(ip, {"method": "getPilot", "params": {}})
        if response and "result" in response:
            self.bulb_info[ip] = response["result"]
            return response["result"]
        return None

    def set_state(self, ip_list: Union[str, List[str]], state: bool) -> None:
        if isinstance(ip_list, str):
            ip_list = [ip_list]
        for ip in ip_list:
            self.send(ip, {"method": "setState", "params": {"state": state}})

    def set_brightness(self, ip_list: Union[str, List[str]], brightness: int) -> None:
        if isinstance(ip_list, str):
            ip_list = [ip_list]
        brightness = max(10, min(100, brightness))
        for ip in ip_list:
            self.send(ip, {"method": "setState", "params": {"state": True, "dimming": brightness}})

    def set_color(self, ip_list: Union[str, List[str]], color: Union[str, Tuple[int, int, int]]) -> None:
        if isinstance(ip_list, str):
            ip_list = [ip_list]
        if isinstance(color, str) and color.lower() in self.TEMP_MAP:
            return self.set_temperature(ip_list, self.TEMP_MAP[color.lower()])
        if isinstance(color, str):
            r, g, b = self.COLOR_MAP.get(color.lower(), self.COLOR_MAP["white"])
        else:
            r, g, b = color
        r, g, b = max(0, min(255, r)), max(0, min(255, g)), max(0, min(255, b))
        for ip in ip_list:
            self.send(ip, {"method": "setState", "params": {"state": True, "r": r, "g": g, "b": b}})

    def set_temperature(self, ip_list: Union[str, List[str]], temp: int) -> None:
        if isinstance(ip_list, str):
            ip_list = [ip_list]
        temp = max(2200, min(6500, temp))
        for ip in ip_list:
            self.send(ip, {"method": "setState", "params": {"state": True, "temp": temp}})

    def rave_mode(self, ip_list: Union[str, List[str]], interval: float = 1.0, colors: List[str] = None) -> None:
        if isinstance(ip_list, str):
            ip_list = [ip_list]
        self.stop_rave()
        self.rave_running = True
        self.rave_thread = threading.Thread(target=self._rave_worker, args=(ip_list, interval, colors), daemon=True)
        self.rave_thread.start()
        print("🎉 Rave Mode ON — Call stop_rave() to quit")

    def _rave_worker(self, ip_list: List[str], interval: float, colors: List[str] = None) -> None:
        color_list = [c for c in (colors or []) if c.lower() in self.COLOR_MAP] or None
        try:
            while self.rave_running:
                for ip in ip_list:
                    if color_list:
                        r, g, b = self.COLOR_MAP[random.choice(color_list).lower()]
                    else:
                        r, g, b = [random.randint(0, 255) for _ in range(3)]
                    self.send(ip, {"method": "setState", "params": {"state": True, "r": r, "g": g, "b": b}})
                time.sleep(interval)
        except Exception as e:
            print(f"Rave mode error: {e}")

    def stop_rave(self) -> None:
        if self.rave_thread and self.rave_running:
            self.rave_running = False
            self.rave_thread.join(2.0)
            print("Rave Mode stopped.")

    def catalog_lights(self):
        if not self.bulbs:
            print("No bulbs discovered. Run discovery first.")
            return
        print("\nStarting light cataloging process...")
        for index, ip in enumerate(self.bulbs):
            print(f"\nIdentifying Light [{index}] at {ip}...")
            for _ in range(3):
                self.set_color(ip, "red")
                time.sleep(0.5)
                self.set_color(ip, "blue")
                time.sleep(0.5)
            name = input("Enter a name for this light: ").strip()
            group = input("Enter a group name (or room): ").strip()
            payload = {
                "lightname": name,
                "group": group,
                "ipaddress": ip
            }
            post_api(payload, "lights")
        print("\n✅ Light cataloging complete.")
