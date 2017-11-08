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

def run_test(sweep, config, iterations, stat_file):
    config_results = []
    for i in range(iterations):
        print('Running scene with config:', config)
        subprocess.check_call(sweep)
        print('Ran scene ...')

        with open(stat_file, 'r') as f:
            json_data = f.read()
            parsed = json.loads(json_data)
            config_results.append(parsed['averageFrameTimeUs'])

    avg, stddev = compute_stddev(config_results)
    return avg, stddev

def main():
    parser = argparse.ArgumentParser(description = 'Script for running automated performance tests.')
    parser.add_argument('--scene',
                        help = 'The glTF/glB scene to test')
    parser.add_argument('--texcomp',
                        help = 'Which texture compression to use for LDR textures',
                        type = str)
    parser.add_argument('--optimized-scene',
                        help = 'Path where a processed scene is placed.',
                        type = str)
    parser.add_argument('--environment-texcomp',
                        help = 'Which texture compression to use for environments',
                        type = str)
    parser.add_argument('--environment-cube',
                        help = 'Cubemap texture',
                        type = str)
    parser.add_argument('--environment-reflection',
                        help = 'Reflection texture',
                        type = str)
    parser.add_argument('--environment-irradiance',
                        help = 'Irradiance texture',
                        type = str)
    parser.add_argument('--extra-lights',
                        help = 'Extra lights',
                        type = str)
    parser.add_argument('--extra-cameras',
                        help = 'Extra cameras',
                        type = str)
    parser.add_argument('--configs',
                        help = 'Which config files to sweep through',
                        type = str,
                        nargs = '+')
    parser.add_argument('--gen-configs',
                        help = 'Automatically generate configs to sweep through',
                        action = 'store_true')
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

    args = parser.parse_args()

    if args.optimized_scene:
        scene_build = ['./tools/gltf-repacker']
        scene_build.append(args.scene)

        scene_build.append('--output')
        scene_build.append(args.optimized_scene)

        if args.texcomp:
            scene_build.append('--texcomp')
            scene_build.append(args.texcomp)

        if args.environment_texcomp:
            scene_build.append('--environment-texcomp')
            scene_build.append(args.environment_texcomp)
        if args.environment_cube:
            scene_build.append('--environment-cube')
            scene_build.append(args.environment_cube)
        if args.environment_reflection:
            scene_build.append('--environment-reflection')
            scene_build.append(args.environment_reflection)
        if args.environment_irradiance:
            scene_build.append('--environment-irradiance')
            scene_build.append(args.environment_irradiance)

        if args.extra_lights:
            scene_build.append('--extra-lights')
            scene_build.append(args.extra_lights)
        if args.extra_cameras:
            scene_build.append('--extra-cameras')
            scene_build.append(args.extra_cameras)

        print('Building scene with arguments', scene_build)
        subprocess.check_call(scene_build)
        print('Built scene ...')

        sweep_path = args.optimized_scene
    else:
        sweep_path = args.scene

    f, stat_file = tempfile.mkstemp()
    f_c, config_file = tempfile.mkstemp()
    os.close(f)
    os.close(f_c)

    if (not args.width) or (not args.height) or (not args.frames):
        sys.stderr.write('Need width, height and frames.\n')
        sys.exit(1)

    base_sweep = ['./viewer/gltf-viewer-headless', '--frames', str(args.frames),
                  '--width', str(args.width),
                  '--height', str(args.height), sweep_path,
                  '--stat', stat_file]

    results = []
    iterations = args.iterations if args.iterations else 1

    if args.configs:
        for config in args.configs:
            sweep = base_sweep + ['--config', config]
            avg, stddev = run_test(sweep, config, iterations, stat_file)
            results.append((config, avg, stddev))
    elif args.gen_configs:
        for renderer in ['forward', 'deferred']:
            for msaa in [1, 4]:
                for prepass in [False, True]:
                    if msaa != 1 and renderer == 'deferred':
                        continue
                    if prepass and renderer == 'deferred':
                        continue
                    for clustered in [False, True]:
                        for hdr_bloom in [False, True]:
                            for shadows in [False, True]:
                                for pos_shadows in [False, True]:
                                    c = {}
                                    c['renderer'] = renderer
                                    c['hdrBloom'] = hdr_bloom
                                    c['msaa'] = msaa
                                    c['clusteredLights'] = clustered
                                    c['directionalLightShadows'] = shadows
                                    c['forwardDepthPrepass'] = prepass
                                    c['clusteredLightsShadows'] = pos_shadows
                                    with open(config_file, 'w') as f:
                                        json.dump(c, f)
                                    sweep = base_sweep + ['--config', config_file]
                                    avg, stddev = run_test(sweep, config_file, iterations, stat_file)

                                    config_name = {}
                                    config_name['renderer'] = renderer
                                    config_name['msaa'] = msaa
                                    config_name['prepass'] = prepass
                                    config_name['clustered'] = clustered
                                    config_name['hdr_bloom'] = hdr_bloom
                                    config_name['shadows'] = shadows
                                    config_name['pos_shadows'] = pos_shadows
                                    results.append((config_name, avg, stddev))

    for res in results:
        print(res)
    os.remove(stat_file)
    os.remove(config_file)

if __name__ == '__main__':
    main()
