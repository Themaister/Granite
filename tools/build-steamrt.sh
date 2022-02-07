#!/bin/bash

# Useful as an initial template.

docker run -it --init --rm -u $UID -w /granite -v $(pwd):/granite registry.gitlab.steamos.cloud/steamrt/scout/sdk ./tools/build-steamrt-inside.sh

