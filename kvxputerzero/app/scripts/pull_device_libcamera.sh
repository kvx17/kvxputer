#!/usr/bin/env bash
# Developer-only: pull Cardputer Zero libcamera runtime libs for cross-linking.
# End users / App Center never run this — the shipped .deb links libcamera
# directly (same as FXCam) and Depends on packages already on the device.
set -euo pipefail

HOST="${1:-kvx@192.168.50.153}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/.cache/device-libcamera/root"
BSP_INC="${ROOT}/.cache/sdk_bsp-src/usr/include/libcamera"

mkdir -p \
  "${DEST}/usr/lib/aarch64-linux-gnu" \
  "${DEST}/usr/include" \
  "${DEST}/usr/lib/aarch64-linux-gnu/pkgconfig"

echo "Host: ${HOST}"
echo "Dest: ${DEST}"

SSH=(ssh -F /dev/null -o StrictHostKeyChecking=accept-new)
SCP=(scp -F /dev/null -o StrictHostKeyChecking=accept-new)

"${SSH[@]}" "${HOST}" 'dpkg -l "*libcamera*" | cat; ls -la /usr/lib/aarch64-linux-gnu/libcamera*.so* 2>/dev/null || true'

"${SCP[@]}" -p \
  "${HOST}:/usr/lib/aarch64-linux-gnu/libcamera.so"* \
  "${HOST}:/usr/lib/aarch64-linux-gnu/libcamera-base.so"* \
  "${DEST}/usr/lib/aarch64-linux-gnu/"

# Prefer soname 0.8 (current Cardputer OS). Drop older copies so the linker
# cannot pick libcamera.so.0.4 by accident.
LIBDIR="${DEST}/usr/lib/aarch64-linux-gnu"
if [[ -e "${LIBDIR}/libcamera.so.0.8" ]]; then
  ln -sfn libcamera.so.0.8 "${LIBDIR}/libcamera.so"
  ln -sfn libcamera-base.so.0.8 "${LIBDIR}/libcamera-base.so"
  rm -f "${LIBDIR}"/libcamera.so.0.4* "${LIBDIR}"/libcamera-base.so.0.4* \
        "${LIBDIR}"/libcamera.so.0.7* "${LIBDIR}"/libcamera-base.so.0.7* || true
fi

# Headers: prefer device libcamera-dev; else matching -dev .deb from RPi archive.
rm -rf "${DEST}/usr/include/libcamera"
if "${SSH[@]}" "${HOST}" 'test -f /usr/include/libcamera/libcamera/camera.h || test -f /usr/include/libcamera/camera.h'; then
  "${SCP[@]}" -pr "${HOST}:/usr/include/libcamera" "${DEST}/usr/include/"
else
  VER="$("${SSH[@]}" "${HOST}" "dpkg-query -W -f='\${Version}' libcamera0.8 2>/dev/null || true")"
  DEB_CACHE="$(cd "$(dirname "$0")/.." && pwd)/.cache/device-libcamera/debs"
  mkdir -p "${DEB_CACHE}"
  DEV_DEB="${DEB_CACHE}/libcamera-dev_${VER}_arm64.deb"
  if [[ -n "${VER}" && ! -f "${DEV_DEB}" ]]; then
    echo "Device has no -dev headers; fetching libcamera-dev ${VER}…"
    curl -fsSL -A 'Debian APT-HTTP/1.3' -o "${DEV_DEB}" \
      "https://archive.raspberrypi.com/debian/pool/main/libc/libcamera/libcamera-dev_${VER}_arm64.deb" \
      || curl -fsSL -A 'Debian APT-HTTP/1.3' -o "${DEV_DEB}" \
      "http://archive.raspberrypi.com/debian/pool/main/libc/libcamera/libcamera-dev_${VER}_arm64.deb" \
      || rm -f "${DEV_DEB}"
  fi
  if [[ -f "${DEV_DEB}" ]]; then
    TMP="$(mktemp -d)"
    (cd "${TMP}" && ar x "${DEV_DEB}" && tar xf data.tar.*)
    mkdir -p "${DEST}/usr/include"
    cp -a "${TMP}/usr/include/libcamera" "${DEST}/usr/include/"
    rm -rf "${TMP}"
  elif [[ -f "${BSP_INC}/camera.h" ]]; then
    echo "WARNING: falling back to BSP headers (ABI may not match 0.8)" >&2
    cp -a "${BSP_INC}" "${DEST}/usr/include/libcamera"
  else
    echo "ERROR: no libcamera headers available" >&2
    exit 1
  fi
fi

if "${SSH[@]}" "${HOST}" 'test -f /usr/lib/aarch64-linux-gnu/pkgconfig/libcamera.pc'; then
  "${SCP[@]}" -p "${HOST}:/usr/lib/aarch64-linux-gnu/pkgconfig/libcamera.pc" \
      "${HOST}:/usr/lib/aarch64-linux-gnu/pkgconfig/libcamera-base.pc" \
      "${DEST}/usr/lib/aarch64-linux-gnu/pkgconfig/" || true
fi

echo
echo "Pulled (build machine only — not shipped to App Center users):"
ls -la "${LIBDIR}"/libcamera*.so* | head
ls "${DEST}/usr/include/libcamera" | head
readelf -d "${LIBDIR}/libcamera.so" | grep SONAME || true
echo
echo "Next: cmake --preset cp0-cross && cmake --workflow --preset cp0-cross-package"
