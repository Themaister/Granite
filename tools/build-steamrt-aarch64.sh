#!/bin/bash

# Useful as an initial template.

docker run -it --init --rm -u $UID -w /granite -v $(pwd):/granite registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk/arm64-on-amd64 ./tools/build-steamrt-inside-aarch64.sh

