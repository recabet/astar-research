#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../report"
python3 ../scripts/render_appendix_tables.py
pdflatex -interaction=nonstopmode report.tex
pdflatex -interaction=nonstopmode report.tex
rm -f report.aux report.log report.out report.snm report.toc
