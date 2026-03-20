#!/bin/bash
set -e

echo "Building FreezeOS..."
cd "$(dirname "$0")"

echo "Cleaning previous build..."
make clean

echo "Compiling..."
make

echo "$(pwd)/freeze.iso"
echo ""
echo ""
echo ""
echo ""
echo "Complete"
