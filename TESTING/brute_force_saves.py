"""
Install:
	pip install pyautogui pyperclip

Run:
	python brute_force_saves.py --length 2000 --delay 3 --key-pause 0
"""

import argparse
import random
import string
import time
from dataclasses import dataclass
from typing import Optional

import pyautogui
import pyperclip


pyautogui.FAILSAFE = True
pyautogui.PAUSE = 0.0

@dataclass
class Action:
	key: str
	ctrl: bool = False
	shift: bool = False
	alt: bool = False
	clipboard_text: Optional[str] = None

	def combo_tuple(self) -> tuple[str, ...]:
		mods = []
		if self.ctrl:
			mods.append("ctrl")
		if self.shift:
			mods.append("shift")
		if self.alt:
			mods.append("alt")
		mods.append(self.key.lower())
		return tuple(mods)

	def describe(self) -> str:
		mods = []
		if self.ctrl:
			mods.append("Ctrl")
		if self.shift:
			mods.append("Shift")
		if self.alt:
			mods.append("Alt")

		if mods:
			text = "+".join(mods) + "+" + self.key
		else:
			text = self.key

		if self.clipboard_text is not None:
			text += f"  [clipboard={self.clipboard_text!r}]"

		return text


def generate_actions(
	length: int,
) -> list[Action]:
	return [
		Action(key="/", ctrl=False),
		Action(key="s", ctrl=True)
	] * length


def press_action(action: Action, key_pause: float) -> None:
	if action.clipboard_text is not None:
		pyperclip.copy(action.clipboard_text)

	keys = []

	if action.ctrl:
		keys.append("ctrl")
	if action.shift:
		keys.append("shift")
	if action.alt:
		keys.append("alt")

	keys.append(action.key)

	if len(keys) == 1:
		pyautogui.press(keys[0])
	else:
		pyautogui.hotkey(*keys)

	if key_pause > 0:
		time.sleep(key_pause)


def replay_actions(actions: list[Action], key_pause: float) -> None:
	for action in actions:
		press_action(action, key_pause)

def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(
		description="Edit and save fast"
	)

	parser.add_argument("--length", type=int, default=250)
	parser.add_argument("--delay", type=float, default=3.0)

	parser.add_argument(
		"--key-pause",
		type=float,
		default=0.0,
		help="Delay between individual key actions. Use 0 for fastest playback.",
	)
	
	return parser.parse_args()

def main() -> None:
	args = parse_args()

	if args.length <= 0:
		raise ValueError("--length must be positive")

	if args.delay < 0:
		raise ValueError("--delay cannot be negative")

	if args.key_pause < 0:
		raise ValueError("--key-pause cannot be negative")
	
	actions = generate_actions(
		length=args.length,
	)

	print(f"Generated {len(actions)} actions.")
	print()
	print("Focus CodeWizard after pressing Enter.")
	print("Move mouse to a screen corner to trigger pyautogui failsafe.")
	print("Press Ctrl+C in this terminal to stop.")
	print()

	replay_count = 0

	while True:
		input("Press Enter to replay the generated sequence... ")

		print(f"Starting in {args.delay} seconds...")
		time.sleep(args.delay)

		replay_count += 1
		print(f"Replay #{replay_count} started.")

		replay_actions(actions, args.key_pause)

		print(f"Replay #{replay_count} finished.")
		print()


if __name__ == "__main__":
	try:
		main()
	except KeyboardInterrupt:
		print("\nStopped.")