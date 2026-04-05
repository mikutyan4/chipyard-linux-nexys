#!/bin/bash
# Setup script for llama2.c with Gemmini
# Downloads the model and tokenizer

set -e

cd "$(dirname "$0")"

echo "=== Downloading llama2.c model and tokenizer ==="

# Download TinyStories 15M model (~15MB)
if [ ! -f "stories15M.bin" ]; then
    echo "Downloading stories15M.bin..."
    wget -q --show-progress https://huggingface.co/karpathy/tinyllamas/resolve/main/stories15M.bin
else
    echo "stories15M.bin already exists"
fi

# Download tokenizer
if [ ! -f "tokenizer.bin" ]; then
    echo "Downloading tokenizer.bin..."
    wget -q --show-progress https://github.com/karpathy/llama2.c/raw/master/tokenizer.bin
else
    echo "tokenizer.bin already exists"
fi

echo ""
echo "=== Setup complete! ==="
echo ""
echo "To build and run:"
echo "  make"
echo "  spike --extension=gemmini pk llama2_gemmini stories15M.bin"
