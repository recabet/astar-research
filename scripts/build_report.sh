#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../report"
pdflatex -interaction=nonstopmode report.tex
pdflatex -interaction=nonstopmode report.tex
rm -f report.aux report.log report.out report.snm report.toc
