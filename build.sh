#!/bin/bash
# build.sh - Compilación de SpectraCore con CMake+Ninja
# Lee configuración desde config.txt, genera estructura de distribución y tarball.

set -e

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

CONFIG_FILE="config.txt"
if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${RED}Error: No se encontró $CONFIG_FILE${NC}"
    exit 1
fi

read_config() {
    local key=$1
    local default=$2
    local value=$(grep -E "^\s*${key}\s*=" "$CONFIG_FILE" | sed -E 's/.*=\s*"?([^"]*)"?.*/\1/' | head -1)
    if [ -z "$value" ]; then
        value="$default"
    fi
    echo "$value"
}

TARGET_OS=$(read_config "TARGET_OS" "linux")
BUILD_TYPE=$(read_config "BUILD_TYPE" "Release")
VERSION=$(read_config "VERSION" "3.0.0")
ARCH=$(read_config "ARCH" "")
BUILD_TESTS=$(read_config "BUILD_TESTS" "true")
BUILD_BENCHMARKS=$(read_config "BUILD_BENCHMARKS" "false")
BUILD_CORES=$(read_config "BUILD_CORES" "0")
EXTRA_CXX_FLAGS=$(read_config "EXTRA_CXX_FLAGS" "")
VERBOSE=$(read_config "VERBOSE" "false")

if [ -z "$ARCH" ]; then
    ARCH=$(uname -m)
    if [ "$ARCH" = "x86_64" ]; then
        ARCH="x86_64"
    elif [ "$ARCH" = "aarch64" ]; then
        ARCH="aarch64"
    else
        ARCH="unknown"
    fi
fi

echo -e "${GREEN}=======================================${NC}"
echo -e "${GREEN} Compilación de SpectraCore con CMake+Ninja${NC}"
echo -e "${GREEN}=======================================${NC}"
echo

echo -e "${GREEN}Configuración leída de $CONFIG_FILE:${NC}"
echo "  TARGET_OS        = $TARGET_OS"
echo "  BUILD_TYPE       = $BUILD_TYPE"
echo "  VERSION          = $VERSION"
echo "  ARCH             = $ARCH"
echo "  BUILD_TESTS      = $BUILD_TESTS"
echo "  BUILD_BENCHMARKS = $BUILD_BENCHMARKS"
echo "  BUILD_CORES      = $BUILD_CORES"
echo "  EXTRA_CXX_FLAGS  = $EXTRA_CXX_FLAGS"
echo "  VERBOSE          = $VERBOSE"
echo

BASE_BUILD_DIR="build/${TARGET_OS}"
mkdir -p "$BASE_BUILD_DIR"

LAST_BUILD=$(find "$BASE_BUILD_DIR" -maxdepth 1 -type d -name "BUILD-*" 2>/dev/null | sort -V | tail -n 1)
if [ -n "$LAST_BUILD" ]; then
    LAST_NUM=$(basename "$LAST_BUILD" | sed 's/BUILD-//')
    if [[ "$LAST_NUM" =~ ^[0-9]+$ ]]; then
        LAST_NUM=$((10#$LAST_NUM))
        NEW_NUM=$((LAST_NUM + 1))
    else
        NEW_NUM=1
    fi
else
    NEW_NUM=1
fi
NEW_NUM_PADDED=$(printf "%02d" $NEW_NUM)
BUILD_DIR="$BASE_BUILD_DIR/BUILD-$NEW_NUM_PADDED"
mkdir -p "$BUILD_DIR"

echo -e "${YELLOW}>> Build nº $NEW_NUM_PADDED -> $BUILD_DIR${NC}"

CACHE_DIR="build/cache"
mkdir -p "$CACHE_DIR"

CMAKE_ARGS="-DCMAKE_BUILD_TYPE=$BUILD_TYPE"

if [ "$BUILD_TESTS" = "true" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DBUILD_TESTS=ON"
else
    CMAKE_ARGS="$CMAKE_ARGS -DBUILD_TESTS=OFF"
fi

if [ "$BUILD_BENCHMARKS" = "true" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DBUILD_BENCHMARKS=ON"
else
    CMAKE_ARGS="$CMAKE_ARGS -DBUILD_BENCHMARKS=OFF"
fi

if [ -n "$EXTRA_CXX_FLAGS" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CXX_FLAGS=\"$EXTRA_CXX_FLAGS\""
fi

echo -e "${YELLOW}>> Configurando CMake...${NC}"
cmake -S . -B "$CACHE_DIR" $CMAKE_ARGS -G Ninja 2>&1 | tee "$BUILD_DIR/build.log"

NINJA_ARGS=""
if [ "$BUILD_CORES" -gt 0 ]; then
    NINJA_ARGS="-j$BUILD_CORES"
fi
if [ "$VERBOSE" = "true" ]; then
    NINJA_ARGS="$NINJA_ARGS -v"
fi

echo -e "${YELLOW}>> Compilando...${NC}"
ninja -C "$CACHE_DIR" $NINJA_ARGS 2>&1 | tee -a "$BUILD_DIR/build.log"

DIST_DIR="$BUILD_DIR/dist"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/include/SpectraCore"
mkdir -p "$DIST_DIR/lib/$TARGET_OS"

cp -r include/SpectraCore/*.h "$DIST_DIR/include/SpectraCore/"
cp -r include/SpectraCore/detail "$DIST_DIR/include/SpectraCore/"

if [ -f "README.md" ]; then
    cp README.md "$DIST_DIR/"
elif [ -f "README.txt" ]; then
    cp README.txt "$DIST_DIR/"
fi

if [ -f "LICENSE-2.0.txt" ]; then
    cp LICENSE-2.0.txt "$DIST_DIR/"
fi

if [ -d "doc" ]; then
    mkdir -p "$DIST_DIR/doc"
    cp -r doc/* "$DIST_DIR/doc/"
fi

cp "$CACHE_DIR/libspectracore.a" "$DIST_DIR/lib/$TARGET_OS/" 2>/dev/null || true
cp "$CACHE_DIR/libspectracore.so" "$DIST_DIR/lib/$TARGET_OS/" 2>/dev/null || true

PACKAGE_NAME="libspectracore-${VERSION}-${TARGET_OS}-${ARCH}"
TARBALL="${PACKAGE_NAME}.tar.gz"

echo -e "${YELLOW}>> Creando paquete $TARBALL...${NC}"
cd "$DIST_DIR"
tar -czf "../$TARBALL" .
cd - > /dev/null

echo -e "${GREEN}✅ Paquete generado: ${BUILD_DIR}/${TARBALL}${NC}"

cat > "$BUILD_DIR/build-info.md" <<EOF
# Build #$NEW_NUM_PADDED - $TARGET_OS
- **Fecha:** $(date)
- **Build Type:** $BUILD_TYPE
- **Versión:** $VERSION
- **Arquitectura:** $ARCH
- **Optimización:** $([ "$BUILD_TYPE" == "Release" ] && echo "-O3 -march=native -mtune=native" || echo "-O0 -g -march=native -mtune=native")
- **Compilación paralela:** $BUILD_CORES núcleos
- **Estado:** ✅ Éxito
- **Paquete:** $TARBALL
EOF

echo -e "${GREEN}✅ Estructura de distribución en: ${DIST_DIR}${NC}"
echo -e "${YELLOW}>> Abriendo carpeta de la build...${NC}"
case "$TARGET_OS" in
    linux)   xdg-open "$BUILD_DIR" 2>/dev/null || true ;;
    macos)   open "$BUILD_DIR" 2>/dev/null || true ;;
    windows) explorer "$BUILD_DIR" 2>/dev/null || start "$BUILD_DIR" 2>/dev/null || true ;;
esac

echo -e "${GREEN}Compilación finalizada.${NC}"
read -p "Presiona Enter para salir..."
