#!/bin/bash

echo "🔍 Check the OS..."

if command -v pacman >/dev/null; then
	echo "🐧 Arch-Based (pacman)"
	sudo pacman -Sy sdl3 mesa libwebp libheif libtiff libjpeg-turbo libjxl libspng

elif command -v apt-get >/dev/null; then
	echo "🐧 Debian/Ubuntu (apt)"
	sudo apt-get update
	sudo apt-get install libsdl3-dev libgl1 libwebp-dev libheif-dev libtiff-dev \
		libjpeg-dev libjxl-dev libspng-dev

elif command -v dnf >/dev/null; then
	echo "🐧 Fedora (dnf)"
	sudo dnf install SDL3-devel mesa-libGL libwebp-devel libheif-devel libtiff-devel \
		libjpeg-turbo-devel libjxl-devel libspng-devel

elif command -v zypper >/dev/null; then
	echo "🐧 openSUSE (zypper)"
	sudo zypper install libSDL3-devel Mesa-libGL-devel libwebp-devel libheif-devel \
		libtiff-devel libjpeg8-devel libjxl-devel libspng-devel

elif command -v brew >/dev/null; then
	echo "🍏 macOS (Homebrew)"
	brew install sdl3 libheif webp jasper libtiff libjpeg-turbo libjxl libspng

else
	echo "❌ I don't know what the system or package manager :c"
	exit 1
fi

echo "✅ Deps installed!"
