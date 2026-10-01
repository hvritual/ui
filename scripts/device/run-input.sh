#!/bin/sh
# Explicit development-only input diagnostic; original board restrictions remain.
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export FRAMEWORK_APPLICATION=coffee-ime
exec "$BASE/run-framework.sh" "$@"
