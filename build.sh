#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

usage() {
    printf '用法：sh build.sh [--help]\n'
    printf '目前建置單一通用安裝包，不需要指定韌體版本。\n'
}

if [ "$#" -gt 0 ]; then
    case "$1" in
        -h|--help)
            if [ "$#" -ne 1 ]; then
                printf '錯誤：--help 不接受其他參數。\n' >&2
                usage >&2
                exit 2
            fi
            usage
            exit 0
            ;;
        -f|--firmware|--firmware=*)
            printf '錯誤：目前建置不區分韌體版本，已停用 %s。\n' "$1" >&2
            usage >&2
            exit 2
            ;;
        *)
            printf '錯誤：不支援的參數 %s。\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
fi

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
    clean all koboroot NT_BUILD=full

exec docker run "$@"
