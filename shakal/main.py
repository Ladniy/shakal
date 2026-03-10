import os
import subprocess

import questionary


def main():
    qtn = questionary

    path_to_input_file = qtn.path("Path to input file").ask()

    preset = qtn.select(
        "Preset",
        choices=["SHAKAL Light", "SHAKAL Medium", "SHAKAL Strong", "SHAKAL ULTRA"],
        use_jk_keys=True,
    ).ask()

    path_to_output_dir = qtn.path("Path to output file", only_directories=True).ask()

    output_file_name = qtn.text("Name for output file").ask()

    output_file = os.path.join(path_to_output_dir, output_file_name)

    if preset == "SHAKAL Light":
        params = [
            "-vcodec",
            "libx265",
            "-crf",
            "22",
            "-preset",
            "slow",
            "-acodec",
            "copy",
        ]

    elif preset == "SHAKAL Medium":
        params = [
            "-vcodec",
            "libx265",
            "-crf",
            "26",
            "-preset",
            "medium",
            "-acodec",
            "aac",
            "-b:a",
            "128k",
        ]

    elif preset == "SHAKAL Strong":
        params = [
            "-vcodec",
            "libx265",
            "-crf",
            "32",
            "-preset",
            "fast",
            "-acodec",
            "aac",
            "-b:a",
            "96k",
        ]

    elif preset == "SHAKAL ULTRA":
        params = [
            "-vf",
            "scale=320:180,fps=12,scale=1920:1080:flags=neighbor",
            "-vcodec",
            "libx265",
            "-crf",
            "42",
            "-preset",
            "ultrafast",
            "-acodec",
            "aac",
            "-b:a",
            "48k",
        ]

    cmd = ["ffmpeg", "-i", path_to_input_file, *params, output_file]

    print("Running:", " ".join(cmd))

    subprocess.run(cmd)


if __name__ == "__main__":
    main()
