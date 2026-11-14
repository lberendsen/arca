#!/bin/sh
# ARCA - Script de compilação rápida autônomo (C++17 / GCC ou Clang)
# Uso:
#   ./compila.sh            -> compila tudo (objetos em ./obj, binários em ./bin)
#   ./compila.sh arca       -> compila apenas o binário principal
#   ./compila.sh limpar     -> remove arquivos de compilação
set -e

DIR_BASE="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR_BASE"

DIR_OBJ="${ARCA_OBJ:-$DIR_BASE/obj}"
DIR_BIN="${ARCA_BIN:-$DIR_BASE/bin}"

FLAGS="-std=c++17 -O2 -pipe -Wall -Wextra -Wno-unused-parameter \
       -Wno-missing-field-initializers -pthread -I. -Inanovg -DNANOVG_GLES2"
LIBS="-lFLAC -lcurl -lcrypto -lsane -lsqlite3 -ljpeg -lavformat -lavcodec -lavutil -lswresample \
      -lasound -lX11 -lEGL -lGLESv2 -lz"

if [ "$1" = "limpar" ]; then
    echo "Limpando diretórios de compilação..."
    rm -rf "$DIR_OBJ" "$DIR_BIN"
    echo "Concluído."
    exit 0
fi

mkdir -p "$DIR_OBJ" "$DIR_BIN"

echo "=== Compilando NanoVG ==="
[ -f "$DIR_OBJ/nanovg.o" ] || g++ $FLAGS -c nanovg/nanovg_gles2.cpp -o "$DIR_OBJ/nanovg.o"

echo "=== Identificando arquivos alterados ==="
MODULOS="Base Modelo Interop Vistas"
ALTERADOS="/tmp/arca_alterados_$$"
trap 'rm -f "$ALTERADOS"' EXIT

for f in $(find $MODULOS -name '*.cpp' 2>/dev/null | sort); do
    o="$DIR_OBJ/$(echo "$f" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$(find $MODULOS -name '*.h' -newer "$o" 2>/dev/null | head -1)" ]; then
        echo "$f"
    fi
done > "$ALTERADOS"

TOTAL=$(wc -l < "$ALTERADOS" | tr -d ' ')
if [ "$TOTAL" -gt 0 ]; then
    echo "Compilando $TOTAL arquivos alterados em paralelo..."
    NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
    xargs -P "$NPROC" -I{} sh -c '
        o="'"$DIR_OBJ"'/"$(echo "{}" | tr / _ | sed "s/\.cpp$/.o/")
        g++ '"$FLAGS"' -c "{}" -o "$o" || { rm -f "$o"; echo "FALHOU: {}"; exit 255; }
    ' < "$ALTERADOS"
else
    echo "Objetos dos módulos estão atualizados."
fi

OBJ_LIB=$(ls "$DIR_OBJ"/*.o 2>/dev/null | grep -v -e '/Testes_' -e '/xos-arca\.o$' -e '/main\.o$')

echo "=== Vinculando executável arca ==="
g++ $FLAGS xos-arca.cpp $OBJ_LIB -o "$DIR_BIN/arca" $LIBS

if [ "$1" != "arca" ]; then
    echo "=== Compilando testes de bancada ==="
    for t in Testes/*.cpp; do
        [ -f "$t" ] || continue
        n=$(basename "$t" .cpp)
        g++ $FLAGS "$t" $OBJ_LIB -o "$DIR_BIN/$n" $LIBS
    done
fi

echo "=== Sucesso! Executáveis gerados em $DIR_BIN ==="
ls -lh "$DIR_BIN"
