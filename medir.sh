#!/bin/bash
# Compila (solo si cambio algo) y empieza a medir.
#
#   ./medir.sh                  mide hasta que se aprieta Ctrl+C
#   ./medir.sh --selftest 100   prueba sin placa: 100 triggers inventados
#
# La configuracion se cambia en src/config.h

cd "$(dirname "$0")" || exit 1

echo "Compilando..."
if ! cmake -S . -B build > build_cmake.log 2>&1; then
   cat build_cmake.log
   echo
   echo "ERROR: no se pudo preparar la compilacion (ver arriba)."
   echo "Si falta cmake o libusb:  sudo apt install build-essential cmake libusb-1.0-0-dev"
   exit 1
fi
rm -f build_cmake.log

if ! cmake --build build -j"$(nproc)" > build/compilacion.log 2>&1; then
   grep -E "error|warning: left operand of comma" build/compilacion.log \
      | sed 's|.*/src/|src/|' | awk '!visto[$0]++' | head -10
   echo
   echo "ERROR: no se pudo compilar. Casi siempre es por un valor mal escrito en"
   echo "       src/config.h (una comilla que falta, una coma en vez de un punto,"
   echo "       un nombre borrado). Arriba dice en que linea esta el problema."
   echo "       El detalle completo quedo en build/compilacion.log"
   exit 1
fi
echo "Listo."
echo

exec ./build/drs_muones "$@"
