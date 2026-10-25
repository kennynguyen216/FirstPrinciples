"""Build the audio playground from the existing acoustics rooms. Needs FFmpeg and Pillow."""

from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from GenerateAcousticsTest import entity, material, sign, entities


ROOT = Path(__file__).resolve().parents[1] / "Projects/Sample/Content"


if __name__ == "__main__":
    original = (ROOT / "Scenes/Acoustics3D.scene").read_text(encoding="utf-8")
    blocks = original.split("\nentity ")
    kept = []
    for block in blocks[1:]:
        if 'Material "Materials/Acoustics/ClapSourceSign.material"' in block:
            continue
        if 'Material "Materials/Acoustics/DopplerSign.material"' in block or 'Material "Materials/Acoustics/HallSign.material"' in block:
            continue
        if 'component AcousticsTest\n' in block:
            block = block.replace('Name "Listener - 1/2/3/4 rooms, Space clap, R dry, T auto, WASD + mouse"', 'Name "Listener - WASD, 1-4 rooms, 5 Doppler, M music"')
            block = block.replace("Position -18 1.65 3", "Position 24 1.65 6").replace("AutoClap true", "AutoClap false")
        if 'component MovingAudio\n' in block:
            block = block.replace('Name "Moving Doppler emitter"', 'Name "MP3 Doppler (5)"')
            block = block.replace('Clip "Audio/DopplerTone.wav"', 'Clip "Audio/PlaygroundDoppler.mp3"')
        kept.append("entity " + block.strip() + "\n")

    material("MusicStation", "0.65 0.25 1", unlit=True)
    entity("MP3 Music (M)", (19, 0.9, 0), (1.2, 1.8, 1.2), components='''component Mesh
  Mesh "Cube"
component Material
  Material "Materials/Acoustics/MusicStation.material"
component AudioSource
  Clip "Audio/PlaygroundMusic.mp3"
  Enabled false
  Volume 0.35
  Pitch 1
  Loop true
  PlayOnStart false
  Bus "Music"
  Spatial false
component Scripts
  script "MusicToggle"
''')
    sign("PlaygroundGuide", "AUDIO PLAYGROUND", [
        "1-4 rooms / SPACE clap / T auto-clap",
        "5 moving MP3 / P Doppler on-off",
        "M music / R dry-wet / B overlap",
        "Import MP3 > select source > Clip"], "#bba2ff", (24, 4.5, -3), 11)
    # New objects need IDs distinct from every room copied from Acoustics3D.
    additions = "\n".join(entities).replace("ac00571c-", "a0d10fab-")
    scene = "# FirstPrinciples scene\nversion 2\n\n" + "\n".join(kept) + "\n" + additions
    (ROOT / "Scenes/AudioPlayground.scene").write_text(scene, encoding="utf-8")

    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(ROOT / "Audio/DopplerTone.wav"),
                    "-ac", "1", "-c:a", "libmp3lame", "-b:a", "96k", str(ROOT / "Audio/PlaygroundDoppler.mp3")], check=True)
    chord = "0.04*(sin(2*PI*110*t)+0.5*sin(2*PI*164.8138*t)+0.4*sin(2*PI*220*t))"
    texture = "0.02*sin(2*PI*329.6276*t)*(0.6+0.4*sin(2*PI*0.2*t))"
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                    f"aevalsrc={chord}+{texture}|{chord}-{texture}:s=24000:d=65", "-af", "afade=t=in:d=0.5,afade=t=out:st=64.5:d=0.5",
                    "-c:a", "libmp3lame", "-b:a", "64k", "-metadata", "title=Original audio playground ambience",
                    str(ROOT / "Audio/PlaygroundMusic.mp3")], check=True)
    assert scene.count("component AudioListener\n") == 1
    assert scene.count("component AudioSource\n") == 6
    assert scene.count("component MovingAudio\n") == 1
    ids = [line.split()[1] for line in scene.splitlines() if line.startswith("entity ")]
    assert len(ids) == len(set(ids))
    print("Generated AudioPlayground, instruction signs, a mono Doppler MP3 and 65-second original stereo ambience.")
