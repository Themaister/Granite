#!/usr/bin/env python3

import sys
import os
import argparse
import json
import copy

def read_stat_file(path):
    with open(path, 'r') as f:
        json_data = f.read()
        parsed = json.loads(json_data)
        return parsed

def default_value(key):
    if key == 'pcf_width':
        return 1
    else:
        return False

def find_run(stat, config):
    keys = [
        'renderer',
        'msaa',
        'prepass',
        'clustered',
        'hdr_bloom',
        'shadows',
        'pos_shadows',
        'stencil_culling',
        'vsm',
        'pcf_width'
        ]

    # This run is irrelevant.
    if config['renderer'] == 'forward' and not config['clustered']:
        return None

    for run in stat['runs']:
        c = run['config']
        equal = True
        for key in keys:
            compare_value = c[key] if key in c else default_value(key)
            config_value = config[key] if key in config else default_value(key)
            if compare_value != config_value:
                equal = False
                break
        if equal:
            return run

    return None

def negative_run(stat, arg, run):
    config = copy.deepcopy(run['config'])
    if arg == 'renderer':
        config[arg] = 'forward'
    else:
        config[arg] = False
    neg_run = find_run(stat, config)
    return neg_run

def shadow_type_to_tag(vsm, pcf):
    if vsm:
        return 'V'
    else:
        return str(pcf)

def main():
    parser = argparse.ArgumentParser(description = 'Script for analyzing difference of changing parameters.')
    parser.add_argument('--stat',
                        help = 'Stat file',
                        type = str)
    parser.add_argument('--config',
                        help = 'Config option to be analyzed',
                        type = str)
    parser.add_argument('--counter',
                        help = 'Counter to be analyzed',
                        type = str,
                        default = 'avg')
    parser.add_argument('--ignore-large-pcf',
                        help = 'Ignore PCF variants larger than 1.',
                        action = 'store_true')

    args = parser.parse_args()
    if args.stat is None:
        sys.exit(1)
    if args.config is None:
        sys.exit(1)

    stat = read_stat_file(args.stat)
    positive_runs = []

    # First, try to find all runs which use our argument.
    for run in stat['runs']:
        if args.ignore_large_pcf:
            if ('pcf_width' in run['config']) and (run['config']['pcf_width'] > 1):
                continue
        if args.config == 'renderer':
            if run['config'][args.config] == 'deferred':
                positive_runs.append(run)
        else:
            if run['config'][args.config]:
                positive_runs.append(run)

    # Then, find the negatives (or None if a negative does not exist).
    negative_runs = [negative_run(stat, args.config, x) for x in positive_runs]

    total_positive = 0.0
    total_negative = 0.0

    gpu_names = '{:<15}'.format('Test')
    gpu_names += '{:>25}'.format(stat['runs'][0]['gpu'] + ' Off')
    gpu_names += '{:>25}'.format(stat['runs'][0]['gpu'] + ' On')
    print(gpu_names)

    has_total = False
    for i in range(len(positive_runs)):
        if negative_runs[i] is None:
            continue

        c = positive_runs[i]['config']

        type_str = ''
        type_str += 'F' if c['renderer'] == 'forward' else 'D'
        type_str += str(c['msaa'])
        type_str += 'Z' if c['prepass'] else 'z'
        type_str += 'C' if c['clustered'] else 'c'
        type_str += 'S' if c['stencil_culling'] else 's'
        type_str += 'H' if c['hdr_bloom'] else 'L'
        type_str += 'SS' if c['shadows'] else 'ss'
        type_str += 'PS' if c['pos_shadows'] else 'ps'
        type_str += shadow_type_to_tag(c['vsm'] if 'vsm' in c else False, c['pcf_width'] if 'pcf_width' in c else 1)
        result_string = '{:15}'.format(type_str)

        neg_run = negative_runs[i]
        pos_run = positive_runs[i]

        has_total = True
        total_positive += pos_run[args.counter]
        total_negative += neg_run[args.counter]

        if args.counter == 'avg':
            result_string += '{:>25}'.format('{:.3f}'.format(neg_run[args.counter] / 1000.0) + ' ms')
            result_string += '{:>25}'.format('{:.3f}'.format(pos_run[args.counter] / 1000.0) + ' ms ' + '({:6.2f} %)'.format(((pos_run[args.counter] - neg_run[args.counter]) / neg_run[args.counter]) * 100.0))
        else:
            result_string += '{:>25}'.format('{:.3f}'.format(neg_run[args.counter] / 1000000.0) + ' M/frame ')
            result_string += '{:>25}'.format('{:.3f}'.format(pos_run[args.counter] / 1000000.0) + ' M/frame ' + '({:6.2f} %)'.format(((pos_run[args.counter] - neg_run[args.counter]) / neg_run[args.counter]) * 100.0))
        print(result_string)

    if has_total:
        result_string = '{:15}'.format('Total')
        if args.counter == 'avg':
            result_string += '{:>25}'.format('{:.3f}'.format(total_negative / 1000.0) + ' ms')
            result_string += '{:>25}'.format('{:.3f}'.format(total_positive / 1000.0) + ' ms ' + '({:6.2f} %)'.format(((total_positive - total_negative) / total_negative) * 100.0))
        else:
            result_string += '{:>25}'.format('{:.3f}'.format(total_negative / 1000000.0) + ' M/frame ')
            result_string += '{:>25}'.format('{:.3f}'.format(total_positive / 1000000.0) + ' M/frame ' + '({:6.2f} %)'.format(((total_positive - total_negative) / total_negative) * 100.0))
        print(result_string)

if __name__ == '__main__':
    main()

