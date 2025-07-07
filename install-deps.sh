#!/bin/bash

# Сheck if packages were installed successfully
verify_install() {
	if [ $? -eq 0 ]; then
		echo "✅ $1 installed successfully"
	return 0
	else
		echo "❌ Failed to install $1"
		return 1
	fi
}

echo "🔍 Detecting OS and package manager..."

# Install packages based on detected package manager
if command -v pacman >/dev/null; then
	echo "🐧 Arch-Based (pacman detected)"
	sudo pacman -Sy --needed --noconfirm sdl3 stb mesa libwebp libheif libtiff libjpeg-turbo libjxl libspng
	yay -Sy sdl3_image
	verify_install "Arch packages" || exit 1

elif command -v apt-get >/dev/null; then
	echo "🐧 Debian/Ubuntu (apt detected)"
	sudo apt-get update -qq
	sudo apt-get install -y libsdl3-dev libsdl3-image-dev libstb-dev libgl1 libwebp-dev \
        libheif-dev libtiff-dev libjpeg-dev libjxl-dev libspng-dev
	verify_install "Debian packages" || exit 1

elif command -v dnf >/dev/null; then
	echo "🐧 Fedora/RHEL (dnf detected)"
	sudo dnf install -y SDL3-devel SDL3_image-devel mesa-libGL stb-devel libwebp-devel \
	libheif-devel libtiff-devel libjpeg-turbo-devel libjxl-devel libspng-devel
	verify_install "Fedora packages" || exit 1

elif command -v zypper >/dev/null; then
	echo "🐧 openSUSE (zypper detected)"
	sudo zypper install -y libSDL3-devel libSDL3_image-devel Mesa-libGL-devel stb libwebp-devel \
	libheif-devel libtiff-devel libjpeg8-devel libjxl-devel libspng-devel
	verify_install "openSUSE packages" || exit 1

elif command -v brew >/dev/null; then
	echo "🍎 macOS (Homebrew detected)"
	brew install sdl3 sdl3_image stb webp libheif libtiff jpeg-turbo jpeg-xl spng
	verify_install "Homebrew packages" || exit 1

else
	echo "❌ Unsupported system or package manager not found :c"
	echo "Please install the following packages manually:"
	echo "- SDL3 and SDL3_image"
	echo "- stb_image"
	echo "- OpenGL libraries"
	echo "- libwebp, libheif, libtiff, libjpeg, libjxl, libspng"
	exit 1
fi

echo "✅ All dependencies installed successfully!"
