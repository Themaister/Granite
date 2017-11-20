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

    args = parser.parse_args()
    if args.stat is None:
        sys.exit(1)

    stats = read_stat_file(args.stat)

    lines = [','.join(['Renderer', 'MSAA', 'Depth prepass', 'Clustered shading', 'Stencil culling', 'HDR bloom', 'Sun shadows', 'Positional shadows', 'Average time ' + stats['runs'][0]['gpu'], 'Stddev ' + stats['runs'][0]['gpu']]) + '\n']
    for run in stats['runs']:
        config = run['config']
        line = [config['renderer'], config['msaa'], config['prepass'], config['clustered'], config['stencil_culling'], config['hdr_bloom'], config['shadows'], config['pos_shadows'], run['avg'], run['stdev']]
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
