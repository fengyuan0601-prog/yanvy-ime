#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Deterministic format/size export only. No creative modification of the masters.
set -euo pipefail
yanvy_asset_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
for yanvy_size in 16 24 32 48 64 128 256 512 1024; do
  convert "$yanvy_asset_dir/yanvy-icon-master-transparent.png" \
    -resize "${yanvy_size}x${yanvy_size}" \
    "$yanvy_asset_dir/yanvy-icon-${yanvy_size}.png"
done
convert "$yanvy_asset_dir/yanvy-icon-master-transparent.png" \
  -define icon:auto-resize=256,128,64,48,32,24,16 \
  "$yanvy_asset_dir/yanvy.ico"
convert "$yanvy_asset_dir/yanvy-icon-master-opaque.png" \
  -resize 1024x1024 -alpha off \
  "$yanvy_asset_dir/yanvy-mobile-1024.png"
