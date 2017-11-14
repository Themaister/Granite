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

def find_run(stats, renderer, msaa, prepass, clustered, stencil_culling, hdr_bloom, shadows, pos_shadows):
    for run in stats['runs']:
        config = run['config']

        # <_>
        if config['renderer'] == renderer:
            if config['msaa'] == msaa:
                if config['prepass'] == prepass:
                    if config['clustered'] == clustered:
                        if config['stencil_culling'] == stencil_culling:
                            if config['hdr_bloom'] == hdr_bloom:
                                if config['shadows'] == shadows:
                                    if config['pos_shadows'] == pos_shadows:
                                        return run
    return None

def main():
    parser = argparse.ArgumentParser(description = 'Script for diffing sweep stat files.')
    parser.add_argument('--stats',
                        help = 'Stat files',
                        nargs = '+')

    args = parser.parse_args()
    if args.stats is None:
        sys.exit(1)

    stats = [read_stat_file(x) for x in args.stats]

    gpu_names = '{:<15}'.format('Test')
    for stat in stats:
        gpu_names += '{:>25}'.format(stat['runs'][0]['gpu'])

    print(gpu_names)

    for renderer in ['forward', 'deferred']:
        for msaa in [1, 4]:
            for prepass in [False, True]:
                if msaa != 1 and renderer == 'deferred':
                        continue
                if prepass and renderer == 'deferred':
                    continue
                for clustered in [False, True]:
                    for stencil_culling in [False, True]:
                        if stencil_culling and (renderer != 'deferred' or clustered):
                            continue
                        for hdr_bloom in [False, True]:
                            for shadows in [False, True]:
                                for pos_shadows in [False, True]:
                                    if pos_shadows and renderer == 'forward' and (not clustered):
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
                                    result_string = '{:15}'.format(type_str)

                                    first = True
                                    reference_time = 0.0

                                    for stat in stats:
                                        run = find_run(stat, renderer, msaa, prepass, clustered, stencil_culling, hdr_bloom, shadows, pos_shadows)
                                        if run is not None:
                                            if first:
                                                reference_time = run['avg']

                                            if not first:
                                                result_string += '{:>25}'.format('{:.3f}'.format(run['avg'] / 1000.0) + ' ms ' + '({:6.2f} %)'.format(((run['avg'] - reference_time) / reference_time) * 100.0))
                                            else:
                                                result_string += '{:>25}'.format('{:.3f}'.format(run['avg'] / 1000.0) + ' ms')

                                            first = False
                                        else:
                                            result_string += '{:>25}'.format('N/A')

                                    print(result_string)


if __name__ == '__main__':
    main()
