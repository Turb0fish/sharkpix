<h1 align=center>🦈 SharkPix</h1>

<p align=center>
 <a href=""><img src="https://img.shields.io/badge/blump-blump-blue"></a>
 <a href=""><img src="https://img.shields.io/badge/EARLY%20ALPHA!-880808"></a>
</p>

<p align=center>
 <a href=""><img src="https://i.ibb.co/YTQHgd4D/screen.png" alt="screen" border="0"></a>
</p>

> This is the default English README. For Russian version, see [README.ru.md](README.ru.md)

A truly *shark* image viewer that aims to be lightweight, without using QT, GTK, etc.

Images are textured and displayed with the ability to zoom, move, and view in full screen. Does not depend on external GUI libraries, uses its own interface built on mouse and keyboard events.

### ⌨️ Controls:
* Arrows, Keypad and 🖱️ - Go to the next or previous image
* Esc - Exit
* F - Full Screen
* Ctrl + 🖱️ - Zoom
* R - Reset Zoom and Center to window size

### Usage:
Run in any directory with images

# 🖼️ Supported formats

PNG and JPEG use libpng and libjpeg-turbo libraries

Format | Status
------------- | :------------:
PNG | ✅
JPEG | ✅
WebP | ✅
FMSHA RUSHA HEIC| ✅
TIFF TIF| ✅
JPEG XL | ✅
BMP | ✅
TGA | ✅
GIF PNG | ⚠️
QOI | ❌
FLIP | ❌

# ⚙️ Build

Gcc is used to build the project, make sure it is on your system
```
git clone https://github.com/Turb0fish/sharkpix
cd sharkpix
bash install-deps.sh
bash compile.sh
./SharkPix
```

# TODO

In progress
