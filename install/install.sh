#!/bin/bash

# Semidiameter service installation script
# Usage: sudo ./install.sh [add|del] [target_directory]

set -e

COMMAND=$1
TARGET_DIR=$2

SERVICE_NAME="radius-proxy"
SERVICE_FILE="${SERVICE_NAME}.service"
SYSTEMD_DIR="/etc/systemd/system"

# Detect paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Files
BIN_NAME="radius_proxy"
SOURCE_BIN="${PROJECT_ROOT}/${BIN_NAME}"
SOURCE_SERVICE="${SCRIPT_DIR}/${SERVICE_FILE}"
SOURCE_INSTALL="${SCRIPT_DIR}/install.sh"

# Check root
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root (use sudo)"
   exit 1
fi

function add_service() {
    if [[ -z "$TARGET_DIR" ]]; then
        echo "Error: Target directory is required"
        echo "Usage: sudo $0 add <target_directory>"
        exit 1
    fi

    # Check if we have the binary
    if [[ ! -f "$SOURCE_BIN" ]]; then
        # Try finding it in current directory as fallback
        if [[ -f "./${BIN_NAME}" ]]; then
            SOURCE_BIN="$(pwd)/${BIN_NAME}"
        else
            echo "Error: Binary '$BIN_NAME' not found in $PROJECT_ROOT"
            echo "Please build it first using 'make'"
            exit 1
        fi
    fi

    echo "--- Installing Semidiameter RADIUS Proxy ---"
    
    # Create target directory and get absolute path
    mkdir -p "$TARGET_DIR"
    TARGET_DIR="$(cd "$TARGET_DIR" && pwd)"
    
    echo "[1/4] Copying 3 files to ${TARGET_DIR}..."
    cp "$SOURCE_BIN" "$TARGET_DIR/"
    cp "$SOURCE_SERVICE" "$TARGET_DIR/"
    cp "$SOURCE_INSTALL" "$TARGET_DIR/"
    
    chmod +x "$TARGET_DIR/${BIN_NAME}"
    chmod +x "$TARGET_DIR/install.sh"

    echo "[2/4] Updating service configuration..."
    # Update binary path in the target service file
    # We replace /usr/local/bin/radius_proxy (default in template) with full path to the new binary
    sed -i "s|/usr/local/bin/${BIN_NAME}|${TARGET_DIR}/${BIN_NAME}|g" "$TARGET_DIR/$SERVICE_FILE"

    echo "[3/4] Registering service in systemd..."
    # Symlink the service file so it's easy to edit in the target dir
    ln -sf "$TARGET_DIR/$SERVICE_FILE" "${SYSTEMD_DIR}/${SERVICE_FILE}"
    systemctl daemon-reload
    systemctl enable "${SERVICE_NAME}"

    echo "[4/4] Starting service..."
    systemctl restart "${SERVICE_NAME}"

    echo "Done! Service '${SERVICE_NAME}' is now running."
    echo "Files installed in: ${TARGET_DIR}"
    echo "You can check logs using: journalctl -u ${SERVICE_NAME} -f"
}

function del_service() {
    echo "--- Removing Semidiameter RADIUS Proxy ---"
    
    # Try to find target dir from symlink if not provided
    if [[ -z "$TARGET_DIR" && -L "${SYSTEMD_DIR}/${SERVICE_FILE}" ]]; then
        LINK_TARGET=$(readlink -f "${SYSTEMD_DIR}/${SERVICE_FILE}")
        TARGET_DIR=$(dirname "$LINK_TARGET")
    fi

    echo "[1/3] Stopping and disabling service..."
    # Silently stop and disable to avoid "Unit not found" noise
    systemctl stop "${SERVICE_NAME}" 2>/dev/null || true
    systemctl disable "${SERVICE_NAME}" 2>/dev/null || true

    echo "[2/3] Removing files..."
    if [[ -L "${SYSTEMD_DIR}/${SERVICE_FILE}" || -f "${SYSTEMD_DIR}/${SERVICE_FILE}" ]]; then
        rm -f "${SYSTEMD_DIR}/${SERVICE_FILE}"
    fi
    
    if [[ -n "$TARGET_DIR" && -d "$TARGET_DIR" ]]; then
        echo "Deleting installation directory: ${TARGET_DIR}"
        rm -rf "$TARGET_DIR"
    else
        echo "Note: Installation directory already gone or not found."
    fi

    echo "[3/3] Reloading systemd..."
    systemctl daemon-reload

    echo "Done! Service completely removed."
}

case "$COMMAND" in
    add)
        add_service
        ;;
    del)
        del_service
        ;;
    *)
        echo "Usage: sudo $0 [add|del] [target_directory]"
        exit 1
        ;;
esac

