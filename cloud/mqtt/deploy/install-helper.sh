#!/usr/bin/env bash
# One-time installation by the VPS administrator, from a reviewed checkout.
set -euo pipefail
[[ "$(id -u)" == 0 ]] || { echo 'Kør som root på VPS' >&2; exit 1; }
[[ "$#" == 1 && "$1" =~ ^[a-z_][a-z0-9_-]*$ ]] || { echo 'Angiv deploy-kontoens brugernavn' >&2; exit 2; }
id "$1" >/dev/null
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
stage="$(mktemp -d)"
trap 'rm -rf -- "$stage"' EXIT
printf '%s ALL=(root) NOPASSWD: /usr/local/bin/aitsm-deploy-ingest\n' "$1" > "$stage/sudoers"
visudo -cf "$stage/sudoers"
install -d -o root -g root -m 755 /usr/local/lib/aitsm-ingest
install -o root -g root -m 644 "$source_dir/deploy_ingest.py" "$source_dir/verify_ingest_outage.py" "$source_dir/questdb_schema.py" /usr/local/lib/aitsm-ingest/
install -o root -g root -m 755 "$source_dir/deploy/aitsm-deploy-ingest" /usr/local/bin/aitsm-deploy-ingest
install -o root -g root -m 440 "$stage/sudoers" /etc/sudoers.d/aitsm-ingest-deploy
visudo -c
