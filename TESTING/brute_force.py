"""
random_key_replay.py

Install:
	pip install pyautogui pyperclip

Run:
	python random_key_replay.py --length 2000 --seed 777 --delay 3 --key-pause 0
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


ALPHANUMERIC_KEYS = list(string.ascii_lowercase + string.digits)

SPECIAL_KEYS = [
	"esc",
	"up",
	"down",
	"left",
	"right",
	"enter",
	"backspace",
]

# Explicit allowlist.
# This prevents ctrl+o / ctrl+p / ctrl+u entirely.
CTRL_KEYS = [
	"c",
	"x",
	"v",
	"z",
	"backspace",
]

# Explicit forbidden combos, normalized as lowercase tuples.
FORBIDDEN_COMBOS = {
	("ctrl", "o"),
	("ctrl", "p"),
	("ctrl", "u"),
	("alt", "a"),
}


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


def is_forbidden(action: Action) -> bool:
	key = action.key.lower()
	
	if action.ctrl and key in {"o", "p", "u", "s", "w"}:
		return True

	if action.alt and key in {"a", "esc", "up", "down", "left", "right", "o"}:
		return True
	
	return False


def random_clipboard_text(rng: random.Random, min_len: int, max_len: int) -> str:
	chars = (
		string.ascii_letters
		+ string.digits
		+ " "
		+ "\n"
		+ "_+-=*/\\()[]{}.,;:'\"<>"
	)

	length = rng.randint(min_len, max_len)
	return "".join(rng.choice(chars) for _ in range(length))


def generate_one_action(
	rng: random.Random,
	paste_probability: float,
	min_paste_len: int,
	max_paste_len: int,
) -> Action:
	while True:
		roll = rng.random()

		# Ctrl-combos.
		if roll < 0.18:
			key = rng.choice(CTRL_KEYS)

			# Keep Ctrl+V as plain Ctrl+V.
			# Shift+Ctrl+V or Alt+Ctrl+V can mean special paste in some programs.
			if key == "v":
				action = Action(key="v", ctrl=True)

				if rng.random() < paste_probability:
					action.clipboard_text = random_clipboard_text(
						rng,
						min_len=min_paste_len,
						max_len=max_paste_len,
					)

				return action

			action = Action(
				key=key,
				ctrl=True,
				shift=rng.random() < 0.15,
				alt=rng.random() < 0.05,
			)

		# Special non-character keys.
		elif roll < 0.38:
			action = Action(
				key=rng.choice(SPECIAL_KEYS),
				shift=rng.random() < 0.12,
				alt=rng.random() < 0.08,
			)

		# Plain alphanumeric keys.
		else:
			action = Action(
				key=rng.choice(ALPHANUMERIC_KEYS),
				shift=rng.random() < 0.25,
				alt=rng.random() < 0.06,
			)

		if not is_forbidden(action):
			return action


def generate_actions(
	length: int,
	seed: int,
	paste_probability: float,
	min_paste_len: int,
	max_paste_len: int,
) -> list[Action]:
	rng = random.Random(seed)

	return [
		generate_one_action(
			rng,
			paste_probability=paste_probability,
			min_paste_len=min_paste_len,
			max_paste_len=max_paste_len,
		)
		for _ in range(length)
	]


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


def write_log(path: str, actions: list[Action], seed: int) -> None:
	with open(path, "w", encoding="utf-8") as f:
		f.write(f"seed={seed}\n")
		f.write(f"length={len(actions)}\n\n")

		for i, action in enumerate(actions):
			f.write(f"{i:06d}: {action.describe()}\n")


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(
		description="Generate and repeatedly replay a deterministic random key sequence."
	)

	parser.add_argument("--length", type=int, default=250)
	parser.add_argument("--seed", type=int, default=12345)
	parser.add_argument("--delay", type=float, default=3.0)

	parser.add_argument(
		"--key-pause",
		type=float,
		default=0.0,
		help="Delay between individual key actions. Use 0 for fastest playback.",
	)

	parser.add_argument(
		"--paste-probability",
		type=float,
		default=0.65,
	)

	parser.add_argument("--min-paste-len", type=int, default=1)
	parser.add_argument("--max-paste-len", type=int, default=80)

	parser.add_argument(
		"--log",
		default="generated_key_sequence.txt",
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

	if not 0.0 <= args.paste_probability <= 1.0:
		raise ValueError("--paste-probability must be between 0 and 1")

	if args.min_paste_len < 0 or args.max_paste_len < args.min_paste_len:
		raise ValueError("Paste length range is invalid")

	actions = generate_actions(
		length=args.length,
		seed=args.seed,
		paste_probability=args.paste_probability,
		min_paste_len=args.min_paste_len,
		max_paste_len=args.max_paste_len,
	)

	write_log(args.log, actions, args.seed)

	print(f"Generated {len(actions)} actions.")
	print(f"Seed: {args.seed}")
	print(f"Action log written to: {args.log}")
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