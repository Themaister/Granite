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

    delta = 0.0
    if args.subtract_blit_overhead:
        for run in stats['runs']:
            if run['method'] == 'none':
                delta = -run['avg']
                break

    lines = [','.join(['Method', 'Average time ' + stats['runs'][0]['gpu']]) + '\n']
    for run in stats['runs']:
        method = run['method']
        line = [method, max(run['avg'] + delta, 0.0)]
        line = [str(x) for x in line]
        lines.append(','.join(line) + '\n')

    if args.output is not None:
        with open(args.output, 'w') as f:
            f.writelines(lines)
    else:
        for line in lines:
            print(line)

if __name__ == '__main__':
    main()
