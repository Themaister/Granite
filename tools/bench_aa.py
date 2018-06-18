#!/usr/bin/env python3

import sys
import os
import argparse
import json
import subprocess
import tempfile
import statistics

def compute_stddev(values):
    stdev = 0.0 if len(values) <= 1 else statistics.stdev(values)
    avg = statistics.mean(values)
    return avg, stdev

def run_test(sweep, method, iterations, stat_file, adb):
    config_results = []
    gpu_cycles = []
    bandwidth_read = []
    bandwidth_write = []
    for _ in range(iterations):
        print('Running AA with method:', method)
        subprocess.check_call(sweep)
        print('Ran scene ...')

        if adb:
            subprocess.check_call(['adb', 'pull', '/data/local/tmp/granite/stat.json', stat_file])

        with open(stat_file, 'r') as f:
            json_data = f.read()
            parsed = json.loads(json_data)
            config_results.append(parsed['averageFrameTimeUs'])
            if 'gpuCycles' in parsed:
                gpu_cycles.append(parsed['gpuCycles'])
            if 'bandwidthRead' in parsed:
                bandwidth_read.append(parsed['bandwidthRead'])
            if 'bandwidthWrite' in parsed:
                bandwidth_write.append(parsed['bandwidthWrite'])

    with open(stat_file, 'r') as f:
        json_data = f.read()
        parsed = json.loads(json_data)
        gpu = parsed['gpu']
        version = parsed['driverVersion']

    avg, stddev = compute_stddev(config_results)
    avg_gpu_cycles = 0.0 if len(gpu_cycles) == 0 else statistics.mean(gpu_cycles)
    avg_bw_read = 0.0 if len(bandwidth_read) == 0 else statistics.mean(bandwidth_read)
    avg_bw_write = 0.0 if len(bandwidth_write) == 0 else statistics.mean(bandwidth_write)
    return avg, stddev, gpu, version, avg_gpu_cycles, avg_bw_read, avg_bw_write

def map_result_to_json(result, width, height, gpu, version):
    return { 'method': result[0], 'avg': result[1], 'stdev': result[2], 'width': width, 'height': height, 'gpu': gpu, 'version': version,
            'gpuCycles': result[3], 'bandwidthRead': result[4], 'bandwidthWrite': result[5] }

def main():
    parser = argparse.ArgumentParser(description = 'Script for running AA benchmark.')
    parser.add_argument('--images',
                        help = 'The 2 input images to test',
                        nargs = '+')
    parser.add_argument('--android-binary',
                        help = 'Path to android binary')
    parser.add_argument('--binary',
                        help = 'Path to binary')
    parser.add_argument('--width',
                        help = 'Resolution X',
                        type = int)
    parser.add_argument('--height',
                        help = 'Resolution Y',
                        type = int)
    parser.add_argument('--frames',
                        help = 'Number of frames',
                        type = int)
    parser.add_argument('--iterations',
                        help = 'Number of iterations',
                        type = int)
    parser.add_argument('--cleanup',
                        help = 'Cleanup stale data after running',
                        action = 'store_true')
    parser.add_argument('--results',
                        help = 'Store results JSON',
                        type = str)
    parser.add_argument('--builtin',
                        help = 'Where to find the builtin assets/shaders',
                        type = str)
    parser.add_argument('--hw-counter-lib',
                        help = 'Helper library for HW counters',
                        type = str)

    args = parser.parse_args()

    if len(args.images) != 2:
        sys.stderr.write('Need --images.\n')
        sys.exit(1)

    sweep_image0 = args.images[0]
    sweep_image1 = args.images[1]

    if args.android_binary is not None:
        if args.builtin is None:
            sys.stderr.write('--builtin must be defined when sweeping on Android.\n')
            sys.exit(1)

        print('Setting up directories ...')
        subprocess.check_call(['adb', 'shell', 'mkdir', '-p', '/data/local/tmp/granite'])
        subprocess.check_call(['adb', 'shell', 'mkdir', '-p', '/data/local/tmp/granite/cache'])
        subprocess.check_call(['adb', 'shell', 'mkdir', '-p', '/data/local/tmp/granite/assets'])
        print('Pushing granite binary ...')
        subprocess.check_call(['adb', 'push', args.android_binary, '/data/local/tmp/granite/aa-bench-headless'])
        subprocess.check_call(['adb', 'shell', 'chmod', '+x', '/data/local/tmp/granite/aa-bench-headless'])

        if args.hw_counter_lib is not None:
            print('Pushing HW counter library.')
            subprocess.check_call(['adb', 'push', args.hw_counter_lib, '/data/local/tmp/granite/hwcounter.so'])

        print('Pushing test scene ...')
        subprocess.check_call(['adb', 'push', sweep_image0, '/data/local/tmp/granite/image0.png'])
        subprocess.check_call(['adb', 'push', sweep_image1, '/data/local/tmp/granite/image1.png'])
        print('Pushing builtin assets ...')

        subprocess.check_call(['adb', 'push', args.builtin, '/data/local/tmp/granite/'])

    f, stat_file = tempfile.mkstemp()
    f_c, config_file = tempfile.mkstemp()
    os.close(f)
    os.close(f_c)

    if (args.width is None) or (args.height is None) or (args.frames is None):
        sys.stderr.write('Need width, height and frames.\n')
        sys.exit(1)

    if args.android_binary is not None:
        base_sweep = ['adb', 'shell', '/data/local/tmp/granite/aa-bench-headless', '--frames', str(args.frames),
                      '--width', str(args.width),
                      '--height', str(args.height), '--input-images', '/data/local/tmp/granite/image0.png', '/data/local/tmp/granite/image1.png',
                      '--stat', '/data/local/tmp/granite/stat.json',
                      '--fs-builtin /data/local/tmp/granite/assets',
                      '--fs-assets /data/local/tmp/granite/assets',
                      '--fs-cache /data/local/tmp/granite/cache']
        if args.hw_counter_lib is not None:
            base_sweep.append('--hw-counter-lib')
            base_sweep.append('/data/local/tmp/granite/hwcounter.so')
    else:
        binary = args.binary if args.binary is not None else './tools/aa-bench-headless'
        base_sweep = [binary, '--frames', str(args.frames),
                      '--width', str(args.width),
                      '--height', str(args.height), '--input-images', sweep_image0, sweep_image1,
                      '--stat', stat_file]
        if args.hw_counter_lib is not None:
            base_sweep.append('--hw-counter-lib')
            base_sweep.append('/data/local/tmp/granite/hwcounter.so')

    results = []
    iterations = args.iterations if args.iterations is not None else 1

    gpu = None
    version = None

    methods = ['none', 'fxaa', 'fxaa2phase', 'smaaLow', 'smaaMedium', 'smaaHigh', 'smaaUltra', 'smaaUltraT2X',
               'taaLow', 'taaMedium', 'taaHigh', 'taaUltra', 'taaExtreme', 'taaNightmare']

    for method in methods:
        sweep = base_sweep + ['--aa-method', method]
        avg, stddev, gpu, version, gpu_cycles, bw_read, bw_write = run_test(sweep, method, iterations, stat_file, args.android_binary is not None)
        results.append((method, avg, stddev, gpu_cycles, bw_read, bw_write))

    for res in results:
        print(res)
    os.remove(stat_file)
    os.remove(config_file)

    if args.results is not None:
        with open(args.results, 'w') as f:
            json.dump({ 'runs': [map_result_to_json(x, args.width, args.height, gpu, version) for x in results] }, f, indent = 4)

    if args.cleanup is not None:
        if args.android_binary is not None:
            subprocess.check_call(['adb', 'shell', 'rm', '-r', '/data/local/tmp/granite'])

if __name__ == '__main__':
    main()
