#!/bin/bash

# Prepare output files
OUTCOME=1
echo "${OUTCOME}" > /home/exit-code.txt
echo "ext-process: Test not run" > /home/test-output.txt

CWD=`pwd`

# Check Boost version >= 1.66
{
BOOST_VER=$(echo -e '#include <boost/version.hpp>\nBOOST_VERSION' | gcc -s -x c++ -E - | grep "^[^#;]")
} &> /dev/null
if [[ "$BOOST_VER" -lt 106600 ]]; then
    # Remove old version
    apt -y remove --purge libboost*
    rm -rf /usr/local/lib/libboost*
    rm -rf /usr/local/include/boost
    rm -rf /usr/local/lib/cmake/[Bb]oost*
    rm -f /usr/lib/libboost_*
    rm -f /usr/lib/x86_64-linux-gnu/libboost_*
    rm -rf /usr/include/boost

    # Install newer Boost from PPA
    apt -y update
    apt -y install software-properties-common
    add-apt-repository -y ppa:mhier/libboost-latest
    apt -y update
    apt -y install libboost1.74-*

    # Ensure ns-3 is pointing at newer Boost libs
    cd "${NS3_DEBUG_DIR}"
    . ${NS3_PY_ENV}/bin/activate
    ./waf configure --boost-includes=/usr/include --boost-libs=/usr/lib/
    deactivate
    cd "${CWD}"
fi

# Install module into ns-3 "contrib" tree using the Makefile (assumption: working directory is the same as this file)
OUTCOME=1
cp -r ext-process /home/
cd /home

# Update path to external process's launcher file in test file
sed -i 's%^\(  std::string launcherPath = \).*$%\1'"\"${NS3_DEBUG_DIR}/contrib/ext-process/launcher-py.sh\";"'%' /home/ext-process/test/ext-process-test-suite.cc

# Ensure debug profile _before_ copying module
export NS3_CURR_PROFILE=${NS3_DEBUG_DIR}
make sync_module FILE=ext-process

# Necessary to avoid killing docker parent processes (build scripts send USR1 signal in interactive shells)
trap "echo Ignoring USR1" USR1
./build-debug.sh && OUTCOME=0

if [[ "$OUTCOME" -eq 1 ]]; then
    echo "Error: build failed"
else
    # Run module-specific tests and retain their outputs
    OUTCOME=1
    make test SUITE=ext-process LOG=/home/test-output && \
    OUTCOME=0

    if [[ "$OUTCOME" -eq 1 ]]; then
        echo "Error: tests failed"
    fi
fi

# Only return success if all previous commands executed correctly
echo "${OUTCOME}" > /home/exit-code.txt

# === No exit here ===
# Avoids exiting the entrypoint sub-script from GitHub action that sources this file
