import os
import subprocess

import questionary


def main():
    qtn = questionary

    path_to_input_file = qtn.path("Path to input file").ask()

    preset = qtn.select("Preset", choices=["SHAKAL Lite"], use_jk_keys=True).ask()

    path_to_output_dir = qtn.path("Path to output file", only_directories=True).ask()

    output_file_name = qtn.text("Name for output file").ask()

    output_file = os.path.join(path_to_output_dir, output_file_name)

    # example promt ffmpeg -i input.mp4 -vcodec libx264 -crf 28 -preset slow -acodec aac -b:a 128k output.mp4
    if preset == "SHAKAL Lite":
        params = [
            "-vcodec",
            "libx264",
            "-crf",
            "28",
            "-preset",
            "slow",
            "-acodec",
            "aac",
            "-b:a",
            "128k",
        ]

    cmd = ["ffmpeg", "-i", path_to_input_file, *params, output_file]

    print("Running:", " ".join(cmd))

    subprocess.run(cmd)


if __name__ == "__main__":
    main()
