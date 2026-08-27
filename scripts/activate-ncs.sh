#!/usr/bin/env bash

# Activate the pinned nRF Connect SDK environment for AITSM Engineering ApS.
# Usage from the project root: source scripts/activate-ncs.sh

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "Source this script instead: source scripts/activate-ncs.sh" >&2
    exit 1
fi

export NCS_HOME="${NCS_HOME:-$HOME/ncs}"
export NCS_VERSION="v3.4.0"
export NCS_TOOLCHAIN="fbf7391cab"
export NCS_ROOT="$NCS_HOME/$NCS_VERSION"
export NCS_TOOLCHAIN_ROOT="$NCS_HOME/toolchains/$NCS_TOOLCHAIN"

if [[ ! -d "$NCS_ROOT" || ! -x "$NCS_TOOLCHAIN_ROOT/usr/local/bin/west" ]]; then
    echo "NCS $NCS_VERSION was not found under $NCS_HOME" >&2
    return 1
fi

export ZEPHYR_BASE="$NCS_ROOT/zephyr"
export ZEPHYR_SDK_INSTALL_DIR="$NCS_TOOLCHAIN_ROOT/opt/zephyr-sdk"
export NRFUTIL_HOME="$NCS_TOOLCHAIN_ROOT/nrfutil/home"

export PATH="$NCS_TOOLCHAIN_ROOT/usr/local/bin:$NCS_TOOLCHAIN_ROOT/opt/zephyr-sdk/hosttools:$NCS_TOOLCHAIN_ROOT/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin:$NCS_ROOT/scripts:$PATH"

echo "Activated NCS $NCS_VERSION (Zephyr 4.4.0)"
echo "ZEPHYR_BASE=$ZEPHYR_BASE"
echo "west=$(command -v west)"
