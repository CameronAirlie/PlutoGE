"""Measure OpenAL's mixer deadline headroom without an output device.

Usage: python tests/benchmark_audio_mixer.py path/to/OpenAL32.dll
Uses the shipped HRTF configuration and the engine's 64-voice limit.
"""
import ctypes as c
import json
import math
import statistics
import sys
import time
from pathlib import Path


def benchmark(path):
    al = c.CDLL(str(Path(path).resolve()))

    def bind(name, result, *arguments):
        function = getattr(al, name)
        function.restype = result
        function.argtypes = arguments
        return function

    open_device = bind('alcLoopbackOpenDeviceSOFT', c.c_void_p, c.c_char_p)
    create = bind('alcCreateContext', c.c_void_p, c.c_void_p, c.POINTER(c.c_int))
    current = bind('alcMakeContextCurrent', c.c_bool, c.c_void_p)
    destroy = bind('alcDestroyContext', None, c.c_void_p)
    close = bind('alcCloseDevice', c.c_bool, c.c_void_p)
    render = bind('alcRenderSamplesSOFT', None, c.c_void_p, c.c_void_p, c.c_int)
    generate_buffers = bind('alGenBuffers', None, c.c_int, c.POINTER(c.c_uint))
    buffer_data = bind('alBufferData', None, c.c_uint, c.c_int, c.c_void_p, c.c_int, c.c_int)
    generate_sources = bind('alGenSources', None, c.c_int, c.POINTER(c.c_uint))
    delete_sources = bind('alDeleteSources', None, c.c_int, c.POINTER(c.c_uint))
    delete_buffers = bind('alDeleteBuffers', None, c.c_int, c.POINTER(c.c_uint))
    source_i = bind('alSourcei', None, c.c_uint, c.c_int, c.c_int)
    source_f = bind('alSourcef', None, c.c_uint, c.c_int, c.c_float)
    source_3f = bind('alSource3f', None, c.c_uint, c.c_int, c.c_float, c.c_float, c.c_float)
    play = bind('alSourcePlayv', None, c.c_int, c.POINTER(c.c_uint))
    error = bind('alGetError', c.c_int)
    device = open_device(None)
    assert device, 'Loopback device unavailable'
    # 48 kHz, stereo float, HRTF enabled, matching AudioSystem::Initialize.
    attributes = (c.c_int * 9)(0x1007, 48000, 0x1990, 0x1501, 0x1991, 0x1406, 0x1992, 1, 0)
    context = create(device, attributes)
    assert context and current(context), 'Loopback context unavailable'
    buffer = c.c_uint()
    sources = (c.c_uint * 64)()
    try:
        samples = (c.c_short * 48000)(*(int(3000 * math.sin(2 * math.pi * 440 * i / 48000)) for i in range(48000)))
        generate_buffers(1, c.byref(buffer))
        buffer_data(buffer.value, 0x1101, samples, c.sizeof(samples), 48000)
        generate_sources(64, sources)
        for index, source in enumerate(sources):
            source_i(source, 0x1009, buffer.value)
            source_i(source, 0x1007, 1)
            source_f(source, 0x100A, 0.01)
            source_3f(source, 0x1004, (index % 8) - 4, 0, -2 - index // 8)
        play(64, sources)
        assert error() == 0, 'OpenAL setup failed'
        output = (c.c_float * 960)()
        elapsed = []
        for index in range(220):
            start = time.perf_counter()
            render(device, output, 480)
            if index >= 20:
                elapsed.append((time.perf_counter() - start) * 1000)
        assert error() == 0, 'OpenAL rendering failed'
        return dict(library=str(path), voices=64, audio_block_ms=10,
                    mean_ms=statistics.mean(elapsed), p95_ms=sorted(elapsed)[189],
                    deadline_misses=sum(value > 10 for value in elapsed), blocks=len(elapsed))
    finally:
        delete_sources(64, sources)
        delete_buffers(1, c.byref(buffer))
        current(None)
        destroy(context)
        close(device)


if __name__ == '__main__':
    print(json.dumps(benchmark(sys.argv[1]), indent=2))
