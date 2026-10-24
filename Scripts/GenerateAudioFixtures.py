"""Generate the project's original audio demo sounds using only the standard library."""

from pathlib import Path
import math
import struct
import wave


ROOT = Path(__file__).resolve().parents[1] / "Projects/Sample"
RATE = 48000


def write_tone(path, seconds, sample):
    frames = round(seconds * RATE)
    samples = [round(max(-1, min(1, sample(i / RATE))) * 32767) for i in range(frames)]
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, RATE, frames, "NONE", "not compressed"))
        output.writeframes(struct.pack(f"<{frames}h", *samples))
    with wave.open(str(path), "rb") as check:
        assert check.getnframes() == frames and check.getnchannels() == 1
        assert check.getsampwidth() == 2 and check.getframerate() == RATE
    assert max(abs(value) for value in samples) < 32767


if __name__ == "__main__":
    # Integer periods make the sustained tone's whole-clip loop continuous.
    write_tone(ROOT / "Content/Audio/Hum.wav", 2,
               lambda t: 0.12 * math.sin(2 * math.pi * 110 * t)
               + 0.04 * math.sin(2 * math.pi * 220 * t))
    write_tone(ROOT / "Content/Audio/Click.wav", 0.16,
               lambda t: 0.35 * math.sin(2 * math.pi * 660 * t)
               * math.exp(-35 * t) * min(1, t / 0.003)
               * min(1, (0.16 - t) / 0.01))
    print("Generated and checked Projects/Sample/Content/Audio/Hum.wav and Click.wav")
