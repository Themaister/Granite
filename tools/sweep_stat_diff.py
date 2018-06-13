#!/usr/bin/env python3

import sys
import os
import argparse
import json

def read_stat_file(path):
    with open(path, 'r') as f:
        json_data = f.read()
        parsed = json.loads(json_data)
        return parsed

def find_run(stats, variant):
    for run in stats['runs']:
        config = run['config']
        if config['variant'] == variant:
            return run
    return None

def shadow_type_to_tag(tag):
    if tag == 0:
        return '1'
    elif tag == 1:
        return '3'
    elif tag == 2:
        return '5'
    else:
        return 'V'

def main():
    parser = argparse.ArgumentParser(description = 'Script for diffing sweep stat files.')
    parser.add_argument('--stats',
                        help = 'Stat files',
                        nargs = '+')
    parser.add_argument('--argument',
                        help = 'Argument for diffing against',
                        type = str,
                        default = 'avg')

    args = parser.parse_args()
    if args.stats is None:
        sys.exit(1)

    stats = [read_stat_file(x) for x in args.stats]

    gpu_names = '{:<15}'.format('Test')
    for stat in stats:
        gpu_names += '{:>25}'.format(stat['runs'][0]['gpu'])

    print(gpu_names)
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

        type_str = ''
        type_str += 'F' if renderer == 'forward' else 'D'
        type_str += str(msaa)
        type_str += 'Z' if prepass else 'z'
        type_str += 'C' if clustered else 'c'
        type_str += 'S' if stencil_culling else 's'
        type_str += 'H' if hdr_bloom else 'L'
        type_str += 'SS' if shadows else 'ss'
        type_str += 'PS' if pos_shadows else 'ps'
        type_str += shadow_type_to_tag(shadow_type)
        result_string = '{:15}'.format(type_str)

        first = True
        reference_time = 0.0

        has_valid = False
        for stat in stats:
            run = find_run(stat, variant)
            if run is not None:
                has_valid = True
                if first:
                    reference_time = run[args.argument]

                if args.argument == 'avg':
                    if not first:
                        result_string += '{:>25}'.format('{:.3f}'.format(run[args.argument] / 1000.0) + ' ms ' + '({:6.2f} %)'.format(((run[args.argument] - reference_time) / reference_time) * 100.0))
                    else:
                        result_string += '{:>25}'.format('{:.3f}'.format(run[args.argument] / 1000.0) + ' ms')
                else:
                    if not first:
                        result_string += '{:>25}'.format('{:.3f}'.format(run[args.argument] / 1000000.0) + ' M/frame ' + '({:6.2f} %)'.format(((run[args.argument] - reference_time) / reference_time) * 100.0))
                    else:
                        result_string += '{:>25}'.format('{:.3f}'.format(run[args.argument] / 1000000.0) + ' M/frame ')

                first = False
            else:
                result_string += '{:>25}'.format('N/A')

        if has_valid:
            print(result_string)

if __name__ == '__main__':
    main()
