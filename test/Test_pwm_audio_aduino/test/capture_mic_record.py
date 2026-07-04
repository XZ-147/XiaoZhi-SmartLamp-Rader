#!/usr/bin/env python3
import argparse
import datetime as dt
import pathlib
import shlex
import sys
import time
import wave

import serial
from serial.tools import list_ports


DEFAULT_BAUD = 921600


def autodetect_port():
    ports = list(list_ports.comports())
    if not ports:
        raise RuntimeError("No serial ports found. Pass --port COMx explicitly.")

    keywords = ("usb", "uart", "cp210", "ch340", "wch", "silicon", "serial")
    for port in ports:
        text = f"{port.device} {port.description} {port.manufacturer}".lower()
        if any(keyword in text for keyword in keywords):
            return port.device
    return ports[0].device


def parse_header(line):
    text = line.decode("utf-8", errors="replace").strip()
    if not text.startswith("MIC_RECORD_BEGIN"):
        return None

    fields = {}
    for token in shlex.split(text[len("MIC_RECORD_BEGIN"):].strip()):
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


def read_exact(ser, byte_count):
    chunks = []
    remaining = byte_count
    while remaining > 0:
        chunk = ser.read(remaining)
        if not chunk:
            raise TimeoutError(f"Timed out with {remaining} bytes still missing")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def save_wav(path, pcm, sample_rate, channels):
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm)


def main():
    parser = argparse.ArgumentParser(
        description="Capture the ESP32 startup MIC_RECORD stream into test/*.raw and test/*.wav."
    )
    parser.add_argument("--port", help="Serial port, for example COM7. Defaults to auto-detect.")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--out-dir", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parent)
    args = parser.parse_args()

    port = args.port or autodetect_port()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Opening {port} at {args.baud}. Reset/power-cycle the board if recording does not start.")
    with serial.Serial(port, args.baud, timeout=args.timeout) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()

        while True:
            line = ser.readline()
            if not line:
                raise TimeoutError("Timed out waiting for MIC_RECORD_BEGIN")

            fields = parse_header(line)
            if fields is None:
                print(line.decode("utf-8", errors="replace").rstrip())
                continue

            byte_count = int(fields["bytes"])
            sample_rate = int(fields["sample_rate"])
            channels = int(fields["channels"])
            source = fields.get("source", "unknown")
            timestamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
            stem = f"mic_{source}_{sample_rate}hz_s16le_{channels}ch_{timestamp}"
            raw_path = args.out_dir / f"{stem}.raw"
            wav_path = args.out_dir / f"{stem}.wav"

            print(f"Capturing {byte_count} bytes -> {raw_path.name}")
            pcm = read_exact(ser, byte_count)
            raw_path.write_bytes(pcm)
            save_wav(wav_path, pcm, sample_rate, channels)

            footer = ser.readline().decode("utf-8", errors="replace").strip()
            if footer:
                print(footer)
            print(f"Saved:\n  {raw_path}\n  {wav_path}")
            return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"capture failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
