#!/usr/bin/env bash

source $CEPH_ROOT/qa/standalone/ceph-helpers.sh

mon_port=$(get_unused_port)

function run() {
    local dir=$1
    shift

    export CEPH_MON="127.0.0.1:$mon_port"
    export CEPH_ARGS
    CEPH_ARGS+="--fsid=$(uuidgen) --auth-supported=none "
    CEPH_ARGS+="--mon-host=$CEPH_MON "
    set -e

    local funcs=${@:-$(set | sed -n -e 's/^\(TEST_[0-9a-z_]*\) .*/\1/p')}
    for func in $funcs ; do
        setup $dir || return 1
	$func $dir || return 1
        teardown $dir || return 1
    done
}

function TEST_bad_inc_map() {
    local dir=$1

    run_mon $dir a
    run_mgr $dir x
    TIMEOUT=60 run_osd $dir 0 --osd-inject-bad-map-crc-probability 1 || return 1
}

main bad-inc-map "$@"
