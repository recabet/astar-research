#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../slides"
for deck in checkpoint defence; do
    pdflatex -interaction=nonstopmode "${deck}.tex"
    pdflatex -interaction=nonstopmode "${deck}.tex"
done
rm -f ./*.aux ./*.log ./*.nav ./*.out ./*.snm ./*.toc
