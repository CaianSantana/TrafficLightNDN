#!/bin/bash


function install_requirements(){
    echo "--- Checking system and installing dependencies... ---"
    if [ -f /etc/os-release ]; then
        source /etc/os-release
    else
        NAME=$(uname -s)
    fi

    case "$NAME" in
        ("Arch Linux")
            echo "--- Arch Linux detected ---"
            sudo pacman -Syu --needed base-devel git libpcap elogind boost python sqlite openssl
            ;;
        ("Ubuntu"|"Debian GNU/Linux")
            echo "--- Debian/Ubuntu detected ---"
            sudo apt-get update && sudo apt-get install -y \
                git build-essential libpcap-dev libsystemd-dev libboost-all-dev libssl-dev libsqlite3-dev pkg-config python3
            ;;
        ("Alpine")
            echo "--- Alpine Linux detected ---"
            sudo apk update && sudo apk add \
                git libpcap-dev elogind-dev build-base boost-dev openssl-dev sqlite-dev pkgconf python3
            ;;
        ("CentOS Stream"|"Fedora Linux"|"Red Hat Enterprise Linux")
            echo "--- Fedora/CentOS/RHEL detected ---"
            sudo dnf install -y \
                git libpcap-devel systemd-devel gcc-c++ boost-devel openssl-devel sqlite-devel pkgconf-pkg-config python3
            ;;
        ("Darwin")
            echo "--- macOS detected ---"
            if ! command -v brew &> /dev/null; then
                echo "Homebrew not found. Please install it first:"
                echo '/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"'
                return 1
            fi
            brew update && brew install --needed git libpcap boost openssl sqlite pkg-config python
            ;;
        (*)
            echo "Operating System '$NAME' is not supported by this script."
            return 1
            ;;
    esac
}

function install_ndn-cxx(){
    cd "${HOME}" || exit
    echo "--- Cloning and installing ndn-cxx... ---"
    git clone https://github.com/named-data/ndn-cxx.git
    cd ndn-cxx/ || exit 1

    ./waf configure
    ./waf
    sudo ./waf install

    if [ "$(uname)" == "Linux" ]; then
        echo "--- Running ldconfig (Linux only) ---"
        sudo ldconfig
    fi
}

function install_NFD(){
    cd "${HOME}" || exit
    echo "--- Cloning and installing NFD... ---"
    git clone --recursive https://github.com/named-data/NFD.git
    cd NFD/ || exit 1

    ./waf configure
    ./waf
    sudo ./waf install

    if [ "$(uname)" == "Linux" ]; then
        echo "--- Running ldconfig (Linux only) ---"
        sudo ldconfig
    fi
}

function main(){
    install_requirements

    install_ndn-cxx

    install_NFD

    echo "--- All components installed successfully! ---"
}

main