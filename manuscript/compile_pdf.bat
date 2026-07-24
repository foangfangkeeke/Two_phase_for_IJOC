@echo off
setlocal

cd /d "%~dp0"

pdflatex -interaction=nonstopmode main.tex
if errorlevel 1 exit /b 1

bibtex main
if errorlevel 1 exit /b 1

pdflatex -interaction=nonstopmode main.tex
if errorlevel 1 exit /b 1

pdflatex -interaction=nonstopmode main.tex
if errorlevel 1 exit /b 1

echo.
echo Build complete: %CD%\main.pdf
