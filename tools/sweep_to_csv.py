#!/usr/bin/env python3

import sys
import os
import argparse
import json
import functools

def read_stat_file(path):
    with open(path, 'r') as f:
        json_data = f.read()
        parsed = json.loads(json_data)
        return parsed

def shadow_type_to_str(cfg):
    if cfg['vsm']:
        return 'VSM'
    else:
        return 'PCF' + str(cfg['pcf_width'])

def compare_run(a, b):
    a_config = a['config']
    b_config = b['config']
    if a_config['configFile'] != b_config['configFile']:
        return -1 if a_config['configFile'] < b_config['configFile'] else 1
    if a_config['maxPointLights'] != b_config['maxPointLights']:
        return -1 if a_config['maxPointLights'] < b_config['maxPointLights'] else 1
    if a_config['maxSpotLights'] != b_config['maxSpotLights']:
        return -1 if a_config['maxSpotLights'] < b_config['maxSpotLights'] else 1
    return 0

def main():
    parser = argparse.ArgumentParser(description = 'Converts a sweep file to CSV.')
    parser.add_argument('--stat',
                        help = 'Stat files',
                        type = str)
    parser.add_argument('--output',
                        help = 'Output path',
                        type = str)
    parser.add_argument('--config-format',
                        help = 'Assume results came from a config-based light sweep',
                        action = 'store_true')

    args = parser.parse_args()
    if args.stat is None:
        sys.exit(1)

    stats = read_stat_file(args.stat)

    if args.config_format:
        entries = [
            'Config',
            'Spot lights',
            'Point lights'
        ]
    else:
        entries = [
                'Renderer',
                'MSAA',
                'Depth prepass',
                'Clustered shading',
                'Stencil culling',
                'HDR bloom',
                'Sun shadows',
                'Positional shadows',
                'Shadow type'
        ]

    entries += [
        'Average time ' + stats['runs'][0]['gpu'],
        'Stddev ' + stats['runs'][0]['gpu'],
        'GPU cycles ' + stats['runs'][0]['gpu'],
        'BW Read bytes ' + stats['runs'][0]['gpu'],
        'BW Write bytes ' + stats['runs'][0]['gpu']
    ]

    lines = [','.join(entries) + ('\n' if args.output is not None else '')]

    sorted_runs = sorted(stats['runs'], key = functools.cmp_to_key(lambda x, y: compare_run(x, y))) if args.config_format else stats['runs']
    for run in sorted_runs:
        config = run['config']

        if args.config_format:
            line = [
                config['configFile'],
                config['maxSpotLights'],
                config['maxPointLights']
            ]
        else:
            line = [
                config['renderer'],
                config['msaa'],
                config['prepass'],
                config['clustered'],
                config['stencil_culling'],
                config['hdr_bloom'],
                config['shadows'],
                config['pos_shadows'],
                shadow_type_to_str(config)
            ]
        line += [
            int(run['avg']),
            int(run['stdev']),
            int(run['gpuCycles']),
            int(run['bandwidthRead']),
            int(run['bandwidthWrite'])
        ]
        line = [str(x) for x in line]
        lines.append(','.join(line) + ('\n' if args.output is not None else ''))

    if args.output is not None:
        with open(args.output, 'w') as f:
            f.writelines(lines)
    else:
        for line in lines:
            print(line)

if __name__ == '__main__':
    main()
