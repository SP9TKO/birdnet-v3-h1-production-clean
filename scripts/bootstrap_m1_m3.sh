#!/usr/bin/env bash
set -euo pipefail

readonly PYTHON_IMAGE='python:3.12.14-slim-bookworm@sha256:8cbe7fcd5df843c789eb26a3d3059859469441633d6e671111f31553e8dc7156'
readonly ARCHIVE_NAME='BirdNET+_V3.0-preview3.1_Global_11K_FP32_Protobuf.zip'
readonly LABELS_NAME='BirdNET+_V3.0-preview3.1_Global_11K_Labels.csv'
readonly ARCHIVE_SHA256='ead54e1c3c0cbf6032def4a5aa6e583b63263440f6975e2dbade7d925e775eeb'
readonly LABELS_SHA256='8124b0ea2d187104c5e2cd95a0f937165647e20349c8fd34d4d5ef991821f8f0'
readonly SAVED_MODEL_SHA256='4700fa91766af07e4923b549727afad9cd94310b01871ac17f717ae00d42631e'
readonly ARCHIVE_URL='https://zenodo.org/api/records/20703646/files/BirdNET+_V3.0-preview3.1_Global_11K_FP32_Protobuf.zip/content'
readonly LABELS_URL='https://zenodo.org/api/records/20703646/files/BirdNET+_V3.0-preview3.1_Global_11K_Labels.csv/content'

repo_root="$(git rev-parse --show-toplevel)"
cd "${repo_root}"

for command_name in curl diff docker git sha256sum unzip; do
  command -v "${command_name}" >/dev/null || {
    printf 'missing required command: %s\n' "${command_name}" >&2
    exit 1
  }
done

verify_sha256() {
  local expected="$1"
  local path="$2"
  printf '%s  %s\n' "${expected}" "${path}" | sha256sum --check --strict -
}

download_verified() {
  local url="$1"
  local output="$2"
  local expected="$3"
  local partial="${output}.part"

  if [[ ! -f "${output}" ]]; then
    mkdir -p "$(dirname "${output}")"
    curl --fail --location --retry 3 --continue-at - --output "${partial}" "${url}"
    verify_sha256 "${expected}" "${partial}"
    mv "${partial}" "${output}"
  fi
  verify_sha256 "${expected}" "${output}"
}

download_verified \
  "${ARCHIVE_URL}" \
  ".m1-work/official/${ARCHIVE_NAME}" \
  "${ARCHIVE_SHA256}"
download_verified \
  "${LABELS_URL}" \
  ".m1-work/official/${LABELS_NAME}" \
  "${LABELS_SHA256}"

# The archive identity is verified above, before extraction or model use.
if [[ ! -f .m1-work/extracted/saved_model.pb ]]; then
  if [[ -e .m1-work/extracted ]]; then
    printf 'refusing to reuse incomplete .m1-work/extracted\n' >&2
    exit 1
  fi
  if [[ -e .m1-work/extracted.partial ]]; then
    printf 'remove or inspect incomplete .m1-work/extracted.partial before retrying\n' >&2
    exit 1
  fi
  mkdir -p .m1-work/extracted.partial
  unzip -q ".m1-work/official/${ARCHIVE_NAME}" -d .m1-work/extracted.partial
  verify_sha256 "${SAVED_MODEL_SHA256}" .m1-work/extracted.partial/saved_model.pb
  mv .m1-work/extracted.partial .m1-work/extracted
fi
verify_sha256 "${SAVED_MODEL_SHA256}" .m1-work/extracted/saved_model.pb

if ! docker image inspect "${PYTHON_IMAGE}" >/dev/null 2>&1; then
  docker pull --platform linux/amd64 "${PYTHON_IMAGE}"
fi

run_in_runtime() {
  docker run --rm \
    --platform linux/amd64 \
    -v "${repo_root}:/workspace" \
    -w /workspace \
    "${PYTHON_IMAGE}" \
    "$@"
}

if [[ ! -f .m2-work/venv/pyvenv.cfg ]]; then
  mkdir -p .m2-work
  run_in_runtime python -m venv .m2-work/venv
fi

python_version="$(run_in_runtime .m2-work/venv/bin/python --version)"
if [[ "${python_version}" != 'Python 3.12.14' ]]; then
  printf 'unexpected Python runtime: %s\n' "${python_version}" >&2
  exit 1
fi

mkdir -p .m2-work/wheels
if ! (
  cd .m2-work/wheels
  sha256sum --check --status ../../requirements/WHEEL_SHA256SUMS.txt
); then
  run_in_runtime \
    .m2-work/venv/bin/python -m pip download \
    --disable-pip-version-check \
    --only-binary=:all: \
    --no-deps \
    --dest .m2-work/wheels \
    --requirement requirements/requirements-frozen.txt
fi
(
  cd .m2-work/wheels
  sha256sum --check --strict ../../requirements/WHEEL_SHA256SUMS.txt
)

run_in_runtime \
  .m2-work/venv/bin/python -m pip install \
  --disable-pip-version-check \
  --no-index \
  --no-deps \
  --find-links .m2-work/wheels \
  --requirement requirements/requirements-frozen.txt
run_in_runtime .m2-work/venv/bin/python -m pip check
run_in_runtime .m2-work/venv/bin/python -m pip freeze --all \
  | diff -u requirements/requirements-frozen.txt -

printf 'M1-M3 bootstrap complete: authoritative inputs and pinned runtime verified.\n'
