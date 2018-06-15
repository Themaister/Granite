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

def main():
    parser = argparse.ArgumentParser(description = 'Converts a sweep file to CSV.')
    parser.add_argument('--stat',
                        help = 'Stat files',
                        type = str)
    parser.add_argument('--output',
                        help = 'Output path',
                        type = str)
    parser.add_argument('--subtract-blit-overhead',
                        help = 'Subtract the time for blitting results (equivalent to "none" method)',
                        action = 'store_true')

    args = parser.parse_args()
    if args.stat is None:
        sys.exit(1)

    stats = read_stat_file(args.stat)

    delta_avg = 0.0
    delta_gpu_cycles = 0.0
    delta_bw_read = 0.0
    delta_bw_write = 0.0
    if args.subtract_blit_overhead:
        for run in stats['runs']:
            if run['method'] == 'none':
                delta_avg = -run['avg']
                delta_gpu_cycles = -run['gpuCycles']
                delta_bw_read = -run['bandwidthRead']
                delta_bw_write = -run['bandwidthWrite']
                break

    entries = [
            'Method',
            'Average time ' + stats['runs'][0]['gpu'],
            'GPU cycles ' + stats['runs'][0]['gpu'],
            'BW read bytes ' + stats['runs'][0]['gpu'],
            'BW write bytes ' + stats['runs'][0]['gpu']
            ]

    lines = [','.join(entries) + ('\n' if args.output is not None else '')]
    for run in stats['runs']:
        method = run['method']
        line = [
                method,
                max(run['avg'] + delta_avg, 0.0),
                max(run['gpuCycles'] + delta_gpu_cycles, 0.0),
                max(run['bandwidthRead'] + delta_bw_read, 0.0),
                max(run['bandwidthWrite'] + delta_bw_write, 0.0)
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
