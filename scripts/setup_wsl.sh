#!/usr/bin/env bash
# Provision a WSL2 Ubuntu 24.04 distro for building OnionHEN.
# Mirrors the packages in Dockerfile; installs the PS5 Payload SDK release zip
# (same source as .github/workflows/nightly-build.yml) into /opt/ps5-payload-sdk.
#
# Run inside WSL:  bash /mnt/c/.../onionHEN-0.9.0-beta1/scripts/setup_wsl.sh
set -euo pipefail

SDK_DIR="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"

echo "==> apt packages"
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  bash build-essential ca-certificates clang-18 cmake curl file git \
  libssl-dev librsvg2-bin lld-18 llvm-18 make ninja-build pkg-config \
  python3 python3-pip python3-pyelftools unzip wget xz-utils
sudo ln -sf /usr/lib/llvm-18/bin/llvm-config /usr/local/bin/llvm-config

if [[ ! -x "${SDK_DIR}/bin/prospero-cmake" ]]; then
  echo "==> PS5 Payload SDK (latest release) -> ${SDK_DIR}"
  tmp="$(mktemp -d)"
  curl -fsSL --retry 3 -o "${tmp}/sdk.zip" \
    https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip
  unzip -q "${tmp}/sdk.zip" -d "${tmp}/out"
  root="$(find "${tmp}/out" -maxdepth 2 -type d -name 'ps5-payload-sdk' | head -1 || true)"
  [[ -z "${root}" && -x "${tmp}/out/bin/prospero-cmake" ]] && root="${tmp}/out"
  [[ -n "${root}" && -x "${root}/bin/prospero-cmake" ]] || { echo "unexpected SDK zip layout" >&2; exit 1; }
  sudo rm -rf "${SDK_DIR}"
  sudo mkdir -p "$(dirname "${SDK_DIR}")"
  sudo cp -a "${root}" "${SDK_DIR}"
  sudo chown -R "$(id -u):$(id -g)" "${SDK_DIR}"
  rm -rf "${tmp}"
else
  echo "==> PS5 Payload SDK already at ${SDK_DIR}"
fi

echo "==> keystone (host unit tests)"
pip3 install --user --break-system-packages --no-cache-dir keystone-engine
kso="$(python3 -c 'import keystone, os; print(os.path.join(os.path.dirname(keystone.__file__), "libkeystone.so"))')"
sudo ln -sf "${kso}" /usr/local/lib/libkeystone.so
sudo ln -sf "${kso}" /usr/local/lib/libkeystone.so.0
sudo ldconfig

profile="${HOME}/.onionhen_env"
cat > "${profile}" <<EOF
export PS5_PAYLOAD_SDK=${SDK_DIR}
export LLVM_CONFIG=/usr/lib/llvm-18/bin/llvm-config
export KEYSTONE_PREFIX=/usr/local
export PATH=/usr/lib/llvm-18/bin:\${PS5_PAYLOAD_SDK}/bin:\${PATH}
EOF
grep -qxF "source ${profile}" "${HOME}/.bashrc" || echo "source ${profile}" >> "${HOME}/.bashrc"

echo
echo "Done. Build with (build dir on the Linux filesystem for speed):"
echo "  source ~/.onionhen_env && ./scripts/build.sh --build-dir ~/onionhen-build --jobs \$(nproc)"
