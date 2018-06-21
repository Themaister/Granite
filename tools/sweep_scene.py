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

def rewrite_config(target, source, spot, point):
    with open(source, 'r') as f:
        json_data = f.read()
        parsed = json.loads(json_data)
        parsed['maxSpotLights'] = spot
        parsed['maxPointLights'] = point
        with open(target, 'w') as f:
            json.dump(parsed, f, indent = 4)

def run_test(sweep, config, iterations, stat_file, sleep, adb):
    config_results = []
    gpu_cycles = []
    bandwidth_read = []
    bandwidth_write = []

    for _ in range(iterations):
        print('Running scene with config:', config)
        subprocess.check_call(sweep)
        print('Ran scene ...')

        if sleep is not None:
            print('Sleeping for {} seconds ...'.format(sleep))
            time.sleep(sleep)

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
    return { 'config': result[0], 'avg': result[1], 'stdev': result[2], 'width': width, 'height': height, 'gpu': gpu, 'version': version,
            'gpuCycles': result[3], 'bandwidthRead': result[4], 'bandwidthWrite': result[5] }

def config_to_path(c):
    res = ''
    res += c['renderer']
    res += '_'
    res += str(c['msaa']) + '_samples'
    res += '_clustering' if c['clusteredLights'] else ''
    res += '_sunshadow' if c['directionalLightShadows'] else ''
    res += '_prepass' if c['forwardDepthPrepass'] else ''
    res += '_positionalshadow' if c['clusteredLightsShadows'] else ''
    res += ('_camera_' + str(c['cameraIndex'])) if ('cameraIndex' in c) else ''
    res += '_hdrbloom' if c['hdrBloom'] else '_ldr'
    res += '_clusterstencil' if c['deferredClusteredStencilCulling'] else ''
    res += '_vsm' if c['directionalLightShadowsVSM'] else ''
    if not c['directionalLightShadowsVSM']:
        res += '_pcf5x5' if c['PCFKernelWidth'] == 5 else ''
        res += '_pcf3x3' if c['PCFKernelWidth'] == 3 else ''
    return res

def main():
    parser = argparse.ArgumentParser(description = 'Script for running automated performance tests.')
    parser.add_argument('--scene',
                        help = 'The glTF/glB scene to test')
    parser.add_argument('--android-viewer-binary',
                        help = 'Path to android binary')
    parser.add_argument('--viewer-binary',
                        help = 'Path to viewer binary')
    parser.add_argument('--repacker-binary',
                        help = 'Path to repacker binary')
    parser.add_argument('--texcomp',
                        help = 'Which texture compression to use for LDR textures',
                        type = str)
    parser.add_argument('--optimized-scene',
                        help = 'Path where a processed scene is placed.',
                        type = str)
    parser.add_argument('--scale',
                        help = 'Scale input scene to optimized scene',
                        type = float)
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
    parser.add_argument('--environment-intensity',
                        help = 'Intensity of the environment',
                        type = float,
                        default = 1.0)
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
    parser.add_argument('--builtin',
                        help = 'Where to find the builtin assets/shaders',
                        type = str)
    parser.add_argument('--gen-configs',
                        help = 'Automatically generate configs to sweep through',
                        action = 'store_true')
    parser.add_argument('--gen-configs-camera-index',
                        help = 'Camera index when using gen-configs',
                        type = int)
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
    parser.add_argument('--png-result-dir',
                        help = 'Store frame results in directory',
                        type = str)
    parser.add_argument('--results',
                        help = 'Store results JSON',
                        type = str)
    parser.add_argument('--quirks',
                        help = 'Run sweep with quirks config',
                        type = str)
    parser.add_argument('--fast',
                        help = 'Run only the most relevant configs',
                        action = 'store_true')
    parser.add_argument('--sleep',
                        help = 'Sleep for seconds in between runs',
                        type = int)
    parser.add_argument('--hw-counter-lib',
                        help = 'Helper library for HW counters',
                        type = str)
    parser.add_argument('--sweep-scene-lights',
                        help = 'Sweep the max light count',
                        action = 'store_true')
    parser.add_argument('--max-spot-lights',
                        help = 'Maximum spot lights when sweeping',
                        type = int,
                        default = 32)
    parser.add_argument('--max-point-lights',
                        help = 'Maximum point lights when sweeping',
                        type = int,
                        default = 32)
    parser.add_argument('--max-pcf-size',
                        help = 'Do not test large PCF kernels larger than certain size.',
                        type = int,
                        default = 5)

    args = parser.parse_args()

    if args.optimized_scene is not None:
        binary = args.repacker_binary if args.repacker_binary is not None else './tools/gltf-repacker'
        scene_build = [binary]
        scene_build.append(args.scene)

        scene_build.append('--output')
        scene_build.append(args.optimized_scene)

        if args.texcomp:
            scene_build.append('--texcomp')
            scene_build.append(args.texcomp)

        if args.environment_texcomp is not None:
            scene_build.append('--environment-texcomp')
            scene_build.append(args.environment_texcomp)
        if args.environment_cube is not None:
            scene_build.append('--environment-cube')
            scene_build.append(args.environment_cube)
        if args.environment_reflection is not None:
            scene_build.append('--environment-reflection')
            scene_build.append(args.environment_reflection)
        if args.environment_irradiance is not None:
            scene_build.append('--environment-irradiance')
            scene_build.append(args.environment_irradiance)
        scene_build.append('--environment-intensity')
        scene_build.append(str(args.environment_intensity))
        if args.scale is not None:
            scene_build.append('--scale')
            scene_build.append(str(args.scale))

        if args.extra_lights is not None:
            scene_build.append('--extra-lights')
            scene_build.append(args.extra_lights)
        if args.extra_cameras is not None:
            scene_build.append('--extra-cameras')
            scene_build.append(args.extra_cameras)

        print('Building scene with arguments', scene_build)
        subprocess.check_call(scene_build)
        print('Built scene ...')

        sweep_path = args.optimized_scene
    else:
        sweep_path = args.scene

    if args.android_viewer_binary is not None:
        if args.builtin is None:
            sys.stderr.write('--builtin must be defined when sweeping on Android.\n')
            sys.exit(1)

        print('Setting up directories ...')
        subprocess.check_call(['adb', 'shell', 'mkdir', '-p', '/data/local/tmp/granite'])
        subprocess.check_call(['adb', 'shell', 'mkdir', '-p', '/data/local/tmp/granite/cache'])
        subprocess.check_call(['adb', 'shell', 'mkdir', '-p', '/data/local/tmp/granite/assets'])
        print('Pushing granite binary ...')
        subprocess.check_call(['adb', 'push', args.android_viewer_binary, '/data/local/tmp/granite/gltf-viewer-headless'])
        subprocess.check_call(['adb', 'shell', 'chmod', '+x', '/data/local/tmp/granite/gltf-viewer-headless'])

        print('Pushing test scene ...')
        subprocess.check_call(['adb', 'push', sweep_path, '/data/local/tmp/granite/scene.glb'])
        print('Pushing builtin assets ...')

        if args.quirks is not None:
            print('Pushing quirks config.')
            subprocess.check_call(['adb', 'push', args.quirks, '/data/local/tmp/granite/quirks.json'])

        if args.hw_counter_lib is not None:
            subprocess.check_call(['adb', 'push', args.hw_counter_lib, '/data/local/tmp/granite/hwcounter.so'])

        subprocess.check_call(['adb', 'push', args.builtin, '/data/local/tmp/granite/'])

        asset_dir = os.path.dirname(sweep_path)
        for dir, subdir, file_list in os.walk(asset_dir):
            for f in file_list:
                if os.path.splitext(f)[1] == '.gtx':
                    print('Pushing texture: ', os.path.join(dir, f), 'to', os.path.basename(f))
                    subprocess.check_call(['adb', 'push', os.path.join(dir, f), '/data/local/tmp/granite/' + os.path.basename(f)])

    f, stat_file = tempfile.mkstemp()
    f_c, config_file = tempfile.mkstemp()
    os.close(f)
    os.close(f_c)

    if (args.configs is None) and (not args.gen_configs):
        print('Not running any configs, exiting.')
        sys.exit(0)

    if (args.width is None) or (args.height is None) or (args.frames is None):
        sys.stderr.write('Need width, height and frames.\n')
        sys.exit(1)

    if args.android_viewer_binary is not None:
        base_sweep = ['adb', 'shell', '/data/local/tmp/granite/gltf-viewer-headless', '--frames', str(args.frames),
                      '--width', str(args.width),
                      '--height', str(args.height), '/data/local/tmp/granite/scene.glb',
                      '--stat', '/data/local/tmp/granite/stat.json',
                      '--fs-builtin /data/local/tmp/granite/assets',
                      '--fs-assets /data/local/tmp/granite/assets',
                      '--fs-cache /data/local/tmp/granite/cache']
        if args.quirks is not None:
            base_sweep.append('--quirks')
            base_sweep.append('/data/local/tmp/granite/quirks.json')
        if args.hw_counter_lib is not None:
            base_sweep.append('--hw-counter-lib')
            base_sweep.append('/data/local/tmp/granite/hwcounter.so')
    else:
        binary = args.viewer_binary if args.viewer_binary is not None else './viewer/gltf-viewer-headless'
        base_sweep = [binary, '--frames', str(args.frames),
                      '--width', str(args.width),
                      '--height', str(args.height), sweep_path,
                      '--stat', stat_file]
        if args.quirks is not None:
            base_sweep.append('--quirks')
            base_sweep.append(args.quirks)
        if args.hw_counter_lib is not None:
            base_sweep.append('--hw-counter-lib')
            base_sweep.append(args.hw_counter_lib)

    results = []
    iterations = args.iterations if args.iterations is not None else 1

    gpu = None
    version = None

    if args.configs is not None:
        light_counts = [(32, 32)]
        if args.sweep_scene_lights:
            light_counts = [(x, 0) for x in range(args.max_spot_lights + 1)] + [(0, x) for x in range(1, args.max_point_lights + 1)]

        for l in light_counts:
            spot_lights = l[0]
            point_lights = l[1]
            for config in args.configs:
                rewrite_config(config_file, config, spot_lights, point_lights)
                if args.android_viewer_binary is not None:
                    sweep = base_sweep + ['--config', '/data/local/tmp/granite/config.json']
                    if args.png_result_dir:
                        sweep.append('--png-reference-path')
                        sweep.append('/data/local/tmp/granite/ref.png')
                    subprocess.check_call(['adb', 'push', config_file, '/data/local/tmp/granite/config.json'])
                else:
                    sweep = base_sweep + ['--config', config_file]
                    if args.png_result_dir:
                        sweep.append('--png-reference-path')
                        sweep.append(os.path.join(args.png_result_dir,
                                                  os.path.splitext(os.path.basename(config))[0]) + '_{}_{}.png'.format(spot_lights, point_lights))

                avg, stddev, gpu, version, gpu_cycles, bw_read, bw_write = run_test(sweep, config_file,
                                                                                    iterations, stat_file, args.sleep,
                                                                                    args.android_viewer_binary is not None)

                if (args.android_viewer_binary is not None) and (args.png_result_dir is not None):
                    subprocess.check_call(['adb', 'pull',
                                           '/data/local/tmp/granite/ref.png',
                                           os.path.join(args.png_result_dir,
                                                        os.path.splitext(os.path.basename(config))[0]) + '_{}_{}.png'.format(spot_lights, point_lights)])

                c = {}
                c['configFile'] = config
                c['maxSpotLights'] = spot_lights
                c['maxPointLights'] = point_lights
                results.append((c, avg, stddev, gpu_cycles, bw_read, bw_write))
    elif args.gen_configs:
        for variant in range(1024):
            renderer = 'forward' if (variant & 512) != 0 else 'deferred'
            msaa = 4 if (variant & 256) != 0 else 1
            prepass = (variant & 128) != 0
            clustered = (variant & 64) != 0
            stencil_culling = (variant & 32) != 0
            hdr_bloom = (variant & 16) != 0
            shadows = (variant & 8) != 0
            pos_shadows = (variant & 4) != 0
            shadow_type = variant & 3

            if msaa != 1 and renderer == 'deferred':
                continue
            if prepass and renderer == 'deferred':
                continue
            if stencil_culling and (renderer != 'deferred' or clustered):
                continue
            if pos_shadows and renderer == 'forward' and (not clustered):
                continue
            if shadow_type != 0 and (not shadows) and (not pos_shadows):
                continue
            if shadow_type == 1 and args.max_pcf_size < 3:
                continue
            if shadow_type == 2 and args.max_pcf_size < 5:
                continue

            if args.fast:
                if (not shadows) or (not pos_shadows):
                    continue
                if shadow_type == 1 or shadow_type == 2:
                    continue
                if renderer == 'deferred' and (not clustered) and (not stencil_culling):
                    continue
                if not hdr_bloom:
                    continue
                if renderer == 'forward' and (not clustered):
                    continue

            c = {}
            c['renderer'] = renderer
            c['hdrBloom'] = hdr_bloom
            c['msaa'] = msaa
            c['clusteredLights'] = clustered
            c['directionalLightShadows'] = shadows
            c['forwardDepthPrepass'] = prepass
            c['clusteredLightsShadows'] = pos_shadows
            c['showUi'] = False
            c['deferredClusteredStencilCulling'] = stencil_culling
            c['directionalLightShadowsVSM'] = shadow_type == 3
            c['clusteredLightsShadowsVSM'] = shadow_type == 3
            if shadow_type == 0 or shadow_type == 3:
                c['PCFKernelWidth'] = 1
            elif shadow_type == 1:
                c['PCFKernelWidth'] = 3
            elif shadow_type == 2:
                c['PCFKernelWidth'] = 5

            if args.gen_configs_camera_index is not None:
                c['cameraIndex'] = args.gen_configs_camera_index
            with open(config_file, 'w') as f:
                json.dump(c, f)

            if args.android_viewer_binary is not None:
                sweep = base_sweep + ['--config', '/data/local/tmp/granite/config.json']
                subprocess.check_call(['adb', 'push', config_file, '/data/local/tmp/granite/config.json'])
                if args.png_result_dir:
                    sweep.append('--png-reference-path')
                    sweep.append('/data/local/tmp/granite/ref.png')
            else:
                sweep = base_sweep + ['--config', config_file]
                if args.png_result_dir:
                    sweep.append('--png-reference-path')
                    sweep.append(os.path.join(args.png_result_dir, config_to_path(c)) + '.png')

            avg, stddev, gpu, version, gpu_cycles, bw_read, bw_write = run_test(sweep, config_file, iterations, stat_file, args.sleep, args.android_viewer_binary is not None)

            if (args.android_viewer_binary  is not None) and (args.png_result_dir is not None):
                subprocess.check_call(['adb', 'pull', '/data/local/tmp/granite/ref.png', os.path.join(args.png_result_dir, config_to_path(c)) + '.png'])

            config_name = {}
            config_name['renderer'] = renderer
            config_name['msaa'] = msaa
            config_name['prepass'] = prepass
            config_name['clustered'] = clustered
            config_name['hdr_bloom'] = hdr_bloom
            config_name['shadows'] = shadows
            config_name['pos_shadows'] = pos_shadows
            config_name['stencil_culling'] = stencil_culling
            config_name['vsm'] = shadow_type == 3
            if shadow_type == 0:
                config_name['pcf_width'] = 1
            elif shadow_type == 1:
                config_name['pcf_width'] = 3
            elif shadow_type == 2:
                config_name['pcf_width'] = 5
            config_name['variant'] = variant

            results.append((config_name, avg, stddev, gpu_cycles, bw_read, bw_write))

    for res in results:
        print(res)
    os.remove(stat_file)
    os.remove(config_file)

    if args.results is not None:
        with open(args.results, 'w') as f:
            json.dump({ 'runs': [map_result_to_json(x, args.width, args.height, gpu, version) for x in results] }, f, indent = 4)

    if args.cleanup is not None:
        if args.android_viewer_binary is not None:
            subprocess.check_call(['adb', 'shell', 'rm', '-r', '/data/local/tmp/granite'])

if __name__ == '__main__':
    main()
