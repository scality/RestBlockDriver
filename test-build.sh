#!/bin/bash -ue

LINUX_DIR=linux
LINUX_CLONES=linux-clones

function die() {
        echo $1
        exit 1
}

function log() {
        echo $@
}

function check_dir() {
        test -d $1 || die "Directory '$1' not found"
}

function checkout_version() {
        local V=$1

        mkdir -p $LINUX_CLONES

        if ! test -d $LINUX_CLONES/linux-$V; then
                log "Removing temporary clone"
                rm -rf $LINUX_CLONES/linux-tmp
                log "Cloning Linux v$V"
                git clone $LINUX_DIR $LINUX_CLONES/linux-tmp
                log "Checking out v$V"
                pushd $LINUX_CLONES/linux-tmp
                git checkout v$V
                popd
                log "Making defconfig and modules_prepare"
                pushd $LINUX_CLONES/linux-tmp
                make defconfig
                make modules_prepare
                popd
                log "Relinking"
                pushd $LINUX_CLONES
                mv linux-tmp linux-$V
                popd
        fi
}

function do_run() {
        local V=$1

        checkout_version $V

        log "Cleaning up..."
        make clean
        log "Building for Linux version $V"
        make KDIR=$LINUX_CLONES/linux-$V
        log "Done"
}

check_dir $LINUX_DIR
check_dir $LINUX_DIR/.git

for i in 13 14 15 16 17 18; do
        do_run 3.$i
done
