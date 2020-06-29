#!/usr/bin/env python3

import sys
import os
import zipfile
import argparse

def main():
    parser = argparse.ArgumentParser(description = 'Script for building a ZIP archive.')
    parser.add_argument('--output',
                        help = 'Path to place the ZIP',
                        required = True)
    parser.add_argument('--input', metavar = ('path', 'zip-path'), type = str, nargs = 2, action = 'append')

    args = parser.parse_args()
    if len(args.input) == 0:
        raise AssertionError('Need at least one input file.')

    with zipfile.ZipFile(args.output, 'w') as z:
        for input in args.input:
            if os.path.isfile(input[0]):
                z.write(input[0], arcname = input[1])
            else:
                for root, dirs, files in os.walk(input[0]):
                    for file in files:
                        fullpath = os.path.join(root, file)
                        rpath = os.path.relpath(fullpath, input[0])
                        z.write(fullpath, arcname = os.path.join(input[1], rpath))
        z.close()

if __name__ == '__main__':
    main()