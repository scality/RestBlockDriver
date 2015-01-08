#!/bin/bash -xue

function die() {
        echo $1
        exit 1
}

function do_ubuntu() {
        sudo aptitude -y install "linux-headers-$(uname -r)"
        sudo aptitude -y install make gcc
}

function do_redhat() {
        sudo yum install -y "kernel-devel-$(uname -r)"
        sudo yum install -y make gcc
}

function detect_platform() {
        if test -e /etc/redhat-release; then
                echo redhat
                return
        fi
        if test "x$(lsb_release -s -i)" = "xUbuntu"; then
                echo ubuntu
                return
        fi

        echo unknown
}

PLATFORM=$(detect_platform)

case $PLATFORM in
        ubuntu)
                do_ubuntu
                ;;
        redhat)
                do_redhat
                ;;
        *)
                die "Unknown platform"
                ;;
esac
