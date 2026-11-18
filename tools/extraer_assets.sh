#!/usr/bin/env bash
# Genera pokestadium.o2r desde tu ROM (US 1.0). Uso: tools/extraer_assets.sh <baserom.z64>
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/generar_yamls_torch.py "$1" yamls/us/rom.yaml assets-config
mv assets-config/config.yml config.yml
./tools/torch-linux o2r -s . -d build "$1"
echo "Listo: build/pokestadium.o2r"
