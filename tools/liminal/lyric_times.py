#!/usr/bin/env python3
"""Place the sung phrases of a song in time with a local speech recogniser, as one input to the song analysis.

The Liminal Euclidean World director plan needs to know where the owner's lyric lines sit, and the mix gives no
direct way to see a voice. This runs faster-whisper on the CPU, on this machine: the audio is decoded locally,
nothing is uploaded, and nothing is written next to the audio. Its output is a JSON list of segments and words with
start and end times, which song_analysis.py and the analyst cross-check against the vocal-band features and the
owner's lyric sheet. A recogniser mishears sung words and invents text in instrumental passages, so treat a result
as a timing hint, never as a transcript.

    tools/liminal/lyric_times.py "~/Desktop/All You Got.wav" --model small.en --out /tmp/lyrics.json

Needs numpy, scipy and `pip install faster-whisper` (it downloads the model to the Hugging Face cache on first use).
"""
import argparse
import json
import os
import sys
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav")
    ap.add_argument("--model", default="small.en")
    ap.add_argument("--out", required=True)
    ap.add_argument("--prompt", default="", help="optional initial prompt (for example lyric vocabulary)")
    ap.add_argument("--beam", type=int, default=5)
    ap.add_argument("--centre", action="store_true", help="recognise the centre-panned channel only")
    args = ap.parse_args()

    import numpy as np
    from faster_whisper import WhisperModel
    from scipy import signal
    from scipy.io import wavfile

    # decode here rather than through the recogniser's own decoder: 16 kHz mono float, or the centre channel only
    # (--centre: L+R with the decorrelated sides gated out, which quietens wide pads under the lead voice)
    sr, x = wavfile.read(os.path.expanduser(args.wav))
    x = x.astype(np.float32) / {np.dtype(np.int32): 2.0 ** 31, np.dtype(np.int16): 2.0 ** 15}.get(x.dtype, 1.0)
    if x.ndim == 2 and args.centre:
        f, t, XL = signal.stft(x[:, 0], sr, nperseg=2048)
        _, _, XR = signal.stft(x[:, 1], sr, nperseg=2048)
        psi = 2 * np.real(XL * np.conj(XR)) / (np.abs(XL) ** 2 + np.abs(XR) ** 2 + 1e-12)
        _, mono = signal.istft(0.5 * (XL + XR) * np.clip(psi, 0, 1) ** 4, sr, nperseg=2048)
        mono = mono[: len(x)]
    else:
        mono = x.mean(1) if x.ndim == 2 else x
    g = np.gcd(int(sr), 16000)
    audio = signal.resample_poly(mono, 16000 // g, int(sr) // g).astype(np.float32)

    t0 = time.time()
    model = WhisperModel(args.model, device="cpu", compute_type="int8")
    segments, info = model.transcribe(
        audio,
        language="en",
        beam_size=args.beam,
        word_timestamps=True,
        vad_filter=False,
        condition_on_previous_text=False,
        initial_prompt=args.prompt or None,
    )
    out = []
    for s in segments:
        words = [{"t0": round(w.start, 2), "t1": round(w.end, 2), "w": w.word.strip(), "p": round(w.probability, 3)}
                 for w in (s.words or [])]
        out.append({"t0": round(s.start, 2), "t1": round(s.end, 2), "text": s.text.strip(),
                    "no_speech": round(s.no_speech_prob, 3), "logprob": round(s.avg_logprob, 3), "words": words})
        print(f"{s.start:7.2f} {s.end:7.2f}  {s.text.strip()}", flush=True)
    with open(args.out, "w") as f:
        json.dump({"model": args.model, "prompt": args.prompt, "segments": out}, f, indent=1)
    print(f"{len(out)} segments in {time.time() - t0:.0f} s -> {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
