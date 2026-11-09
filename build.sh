#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
NT_FIRMWARE=${NT_FIRMWARE:-4.46.23852}

DOCKER_VOLUME_DIR=$SCRIPT_DIR
set -- --platform=linux/amd64

if [ -n "${MSYSTEM:-}" ]; then
    # Git Bash rewrites POSIX paths in Docker arguments (for example /work).
    DOCKER_VOLUME_DIR=$(cygpath -m "$SCRIPT_DIR")
    export MSYS_NO_PATHCONV=1
else
    set -- "$@" "--user=$(id -u):$(id -g)" --env=HOME
fi

set -- "$@" \
    "--volume=$DOCKER_VOLUME_DIR:/work" \
    --workdir=/work \
    --entrypoint=make \
    --rm \
    ghcr.io/pgaskin/nickeltc:1.0 \
    clean all koboroot NT_BUILD=full "NT_FIRMWARE=$NT_FIRMWARE"

exec docker run "$@"
