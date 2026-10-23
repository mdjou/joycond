# joycond build recipes. List them with `just -l`.
default := 'build'

# Configure an out-of-source Release build in build/
configure:
    cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build (runs configure first, cheap if nothing changed)
build: configure
    cmake --build build --parallel

# Debug build with -Werror, in a separate tree so Release objects survive
debug:
    cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
    cmake --build build-debug --parallel

# Install binary, udev rules, systemd unit (requires sudo)
install: build
    sudo make -C build install

# Remove installed files (requires sudo); disables the service first
uninstall:
    sudo systemctl disable --now joycond
    sudo make -C build uninstall

# Build an arm64 .deb (requires fakeroot + dpkg-deb)
deb:
    ./build_deb.sh

# Wipe the build trees
clean:
    rm -rf build build-debug
