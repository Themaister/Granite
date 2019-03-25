#!/usr/bin/env python3

import os
import sys
import argparse
from shutil import copyfile, copytree, rmtree

def canonical_path(p):
    if not os.path.isabs(p):
        p = os.path.join(os.getcwd(), p)
    return p

def transform_gradle_path(p):
    return ':' + p.replace('/', ':').replace('\\', ':')

def find_relative_path(from_path, to_path):
    from_path = os.path.dirname(canonical_path(from_path))
    to_path = canonical_path(to_path)
    relpath = os.path.relpath(to_path, from_path)
    return relpath

def main():
    parser = argparse.ArgumentParser(description = 'Script for automatically creating Android Gradle builds')
    parser.add_argument('--output-gradle',
                        help = 'Path where the Gradle build shall be placed',
                        required = True,
                        default = 'app')
    parser.add_argument('--resource-dir',
                        help = 'Directory where the application provided res/drawable-* folders are found',
                        type = str)
    parser.add_argument('--activity-icon-drawable',
                        help = 'The name of the logo drawable ID found in the res/ folders',
                        type = str,
                        default = 'icon')
    parser.add_argument('--application-id',
                        required = True,
                        help = 'The application app ID, like com.foo.shinyapp')
    parser.add_argument('--granite-dir',
                        help = 'Path to a Granite checkout',
                        default = 'granite')
    parser.add_argument('--native-target',
                        help = 'The CMake target to build',
                        required = True,
                        type = str)
    parser.add_argument('--app-name',
                        help = 'The app name, as shown to the user',
                        type = str,
                        required = True)
    parser.add_argument('--abis',
                        nargs = '+',
                        help = 'Add native ABIs. Default is just arm64-v8a.',
                        type = str)
    parser.add_argument('--version-code',
                        help = 'Version code',
                        type = str,
                        default = "1")
    parser.add_argument('--version-name',
                        help = 'Version name',
                        type = str,
                        default = "1.0")
    parser.add_argument('--cmake-lists-toplevel',
                        help = 'Path to the top-level CMakeLists.txt',
                        type = str,
                        default = 'CMakeLists.txt')
    parser.add_argument('--assets',
                        help = 'The assets folder of the app',
                        type = str,
                        default = 'assets')
    parser.add_argument('--audio',
                        help = 'Enable audio support',
                        action = 'store_true')
    parser.add_argument('--physics',
                        help = 'Enable physics support',
                        action = 'store_true')

    args = parser.parse_args()
    abis = ['arm64-v8a'] if args.abis is None else args.abis

    gradle_base = os.path.join(args.granite_dir, 'application/platforms/android/gradle/')
    if not os.path.isdir(gradle_base):
        print('Cannot find', gradle_base, 'in file system.', file = sys.stderr)
        sys.exit(1)

    manifest = os.path.join(gradle_base, 'AndroidManifest.xml')
    build_gradle = os.path.join(gradle_base, 'build.gradle')
    settings_gradle = os.path.join(gradle_base, 'settings.gradle')
    toplevel_gradle = os.path.join(gradle_base, 'toplevel.build.gradle')
    if (not os.path.isfile(manifest)) or \
        (not os.path.isfile(build_gradle)) or \
        (not os.path.isfile(settings_gradle)) \
        or (not os.path.isfile(toplevel_gradle)):
        print('Cannot find template files in file system.', file = sys.stderr)
        sys.exit(1)

    os.makedirs(args.output_gradle, exist_ok = True)
    os.makedirs(os.path.join(args.output_gradle, 'res'), exist_ok = True)
    os.makedirs(os.path.join(args.output_gradle, 'res/values'), exist_ok = True)
    output_toplevel_build_gradle = 'build.gradle'
    output_settings_gradle = 'settings.gradle'

    resource_dir = os.path.join(args.granite_dir, 'application/platforms/android/gradle/res') if not args.resource_dir else args.resource_dir
    granite_android_activity = find_relative_path(output_settings_gradle, os.path.join(args.granite_dir,
                                                                                       'application/platforms/android'))

    # Write out AndroidManifest.xml
    with open(manifest, 'r') as f:
        manifest_data = f.read()
        manifest_data = manifest_data \
            .replace('$$PACKAGE$$', args.application_id) \
            .replace('$$ICON$$', args.activity_icon_drawable) \
            .replace('$$NATIVE_TARGET$$', args.native_target) \
            .replace('$$VERSION_CODE$$', args.version_code) \
            .replace('$$VERSION_NAME$$', args.version_name)

        target_manifest_path = os.path.join(args.output_gradle, 'AndroidManifest.xml')
        with open(target_manifest_path, 'w') as dump_file:
            print(manifest_data, file = dump_file)

    # Write out strings.xml
    with open(os.path.join(args.output_gradle, 'res/values/strings.xml'), 'w') as f:
        print('<?xml version="1.0" encoding="utf-8"?>', file = f)
        print('<resources>', file = f)
        print('\t<string name="app_name">{}</string>'.format(args.app_name), file = f)
        print('</resources>', file = f)

    # Write out build.gradle
    with open(build_gradle, 'r') as f:
        target_build_gradle = os.path.join(args.output_gradle, 'build.gradle')
        data = f.read()

        cmakelists = find_relative_path(target_build_gradle, args.cmake_lists_toplevel)
        assets = find_relative_path(target_build_gradle, args.assets)
        granite_assets = find_relative_path(target_build_gradle, os.path.join(args.granite_dir, 'assets'))
        external_jni = find_relative_path(target_build_gradle, os.path.join(args.granite_dir,
                                                                            'application/platforms/android/external_layers'))

        target_abis = ', '.join(["'" + x + "'" for x in abis])

        data = data \
            .replace('$$TARGET$$', args.native_target) \
            .replace('$$CMAKELISTS$$', cmakelists) \
            .replace('$$ASSETS$$', assets) \
            .replace('$$GRANITE_ASSETS$$', granite_assets) \
            .replace('$$EXTERNAL_JNI$$', external_jni) \
            .replace('$$ABIS$$', target_abis) \
            .replace('$$AUDIO$$', 'ON' if args.audio else 'OFF') \
            .replace('$$PHYSICS$$', 'ON' if args.physics else 'OFF')

        with open(target_build_gradle, 'w') as dump_file:
            print(data, file = dump_file)


    copyfile(toplevel_gradle, output_toplevel_build_gradle)

    # Write out settings.gradle
    with open(settings_gradle, 'r') as f:
        data = f.read()
        granite_app = find_relative_path(output_settings_gradle, args.output_gradle)
        granite_app = transform_gradle_path(granite_app)

        data = data \
            .replace('$$APP$$', granite_app) \
            .replace('$$GRANITE_ANDROID_ACTIVITY_PATH$$', granite_android_activity)

        with open(output_settings_gradle, 'w') as dump_file:
            print(data, file = dump_file)

    drawables = [
        'drawable-mdpi',
        'drawable-hdpi',
        'drawable-xhdpi',
        'drawable-xxhdpi',
        'drawable-xxxhdpi'
    ]
    target_res = os.path.join(args.output_gradle, 'res')
    for drawable in drawables:
        src = os.path.join(resource_dir, drawable)
        dst = os.path.join(target_res, drawable)
        if os.path.isdir(src):
            if os.path.isdir(dst):
                rmtree(dst)
            copytree(src, dst)

if __name__ == '__main__':
    main()
