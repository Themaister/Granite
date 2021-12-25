#!/usr/bin/env python3

import sys
import time
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

def run_test(sweep, config, iterations, stat_file):
    config_results = []

    for _ in range(iterations):
        print('Running scene with config:', config)
        subprocess.check_call(sweep)
        print('Ran scene ...')

        with open(stat_file, 'r') as f:
            json_data = f.read()
            parsed = json.loads(json_data)
            config_results.append(parsed['averageFrameTimeUs'])

    with open(stat_file, 'r') as f:
        json_data = f.read()
        parsed = json.loads(json_data)
        gpu = parsed['gpu']
        version = parsed['driverVersion']
        perf = parsed['performance'] if 'performance' in parsed else None

    avg, stddev = compute_stddev(config_results)
    return avg, stddev, gpu, version, perf

def map_result_to_json(result, width, height, gpu, version):
    return { 'config': result[0], 'avg': result[1], 'stdev': result[2],
            'width': width, 'height': height, 'gpu': gpu, 'version': version,
            'performance': {} if result[3] is None else result[3] }

def main():
    parser = argparse.ArgumentParser(description = 'Script for running automated performance tests.')
    parser.add_argument('--scene',
                        help = 'The glTF/glB scene to test')
    parser.add_argument('--viewer-binary',
                        help = 'Path to viewer binary')
    parser.add_argument('--configs',
                        help = 'Which config files to sweep through',
                        type = str,
                        nargs = '+')
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
    parser.add_argument('--png-result-dir',
                        help = 'Store frame results in directory',
                        type = str)
    parser.add_argument('--results',
                        help = 'Store results JSON',
                        type = str)
    parser.add_argument('--timestamp',
                        help = 'Measure per pass timestamps',
                        action = 'store_true')
    parser.add_argument('--camera-index',
                        type = int,
                        default = -1,
                        help = 'Camera index')

    args = parser.parse_args()

    f, stat_file = tempfile.mkstemp()
    os.close(f)

    if args.configs is None:
        print('Not running any configs, exiting.')
        sys.exit(0)

    if (args.width is None) or (args.height is None) or (args.frames is None):
        sys.stderr.write('Need width, height and frames.\n')
        sys.exit(1)

    binary = args.viewer_binary if args.viewer_binary is not None else './viewer/gltf-viewer-headless'
    base_sweep = [binary, args.scene,
            '--frames', str(args.frames),
            '--width', str(args.width),
            '--height', str(args.height),
            '--stat', stat_file]
    if args.timestamp:
        base_sweep.append('--timestamp')
    if args.camera_index >= 0:
        base_sweep.append('--camera-index')
        base_sweep.append(str(args.camera_index))

    results = []
    iterations = args.iterations if args.iterations is not None else 1

    gpu = None
    version = None
    for config in args.configs:
        sweep = base_sweep + ['--config', config]
        base_config = os.path.splitext(os.path.basename(config))[0]

        if args.png_result_dir:
            sweep.append('--png-reference-path')
            sweep.append(os.path.join(args.png_result_dir, base_config + '.png'))

        avg, stddev, gpu, version, perf = run_test(sweep, base_config, iterations, stat_file)

        results.append((base_config, avg, stddev, perf))

    for res in results:
        print(res)
    os.remove(stat_file)

    if args.results is not None:
        with open(args.results, 'w') as f:
            json.dump({ 'runs': [map_result_to_json(x, args.width, args.height, gpu, version) for x in results] }, f, indent = 4)

if __name__ == '__main__':
    main()
