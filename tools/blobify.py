#!/usr/bin/env python3

import sys
import os
import argparse
import struct

def main():
    parser = argparse.ArgumentParser(description = 'Script for building a Blobby archive.')
    parser.add_argument('--output',
                        help = 'Path to place the Blob',
                        required = True)
    parser.add_argument('--input', metavar = ('path', 'blob-path'), type = str, nargs = 2, action = 'append')

    args = parser.parse_args()
    if len(args.input) == 0:
        raise AssertionError('Need at least one input file.')

    byte_offset = 0
    archive_files = []

    for input in args.input:
        if os.path.isfile(input[0]):
            size = os.path.getsize(input[0])
            archive_files.append((input[0], input[1], byte_offset, size))
            byte_offset += size
        else:
            for root, dirs, files in os.walk(input[0]):
                for file in files:
                    fullpath = os.path.join(root, file)
                    size = os.path.getsize(fullpath)
                    rpath = os.path.relpath(fullpath, input[0])
                    archive_files.append((fullpath, os.path.join(input[1], rpath), byte_offset, size))
                    byte_offset += size

    with open(args.output, 'wb') as f:
        f.write('BLOBBY01'.encode('ascii'))
        for entry in archive_files:
            f.write('ENTR'.encode('ascii'))
            encoded_path = entry[1].encode('utf-8')
            if len(encoded_path) > 255:
                raise RuntimeError('Path has max length of 255.')
            f.write(struct.pack('B', len(encoded_path)))
            f.write(encoded_path)
            f.write(struct.pack('<Q', entry[2]))
            f.write(struct.pack('<Q', entry[3]))
        f.write('DATA'.encode('ascii'))
        for entry in archive_files:
            with open(entry[0], 'rb') as fr:
                bytes = fr.read()
                f.write(bytes)


if __name__ == '__main__':
    main()