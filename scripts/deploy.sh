#!/bin/bash

# Full name of this scrip
RP=$(realpath $0)

# The directory where this script is placed
RD=$(dirname ${RP})

# The deployment status. We need to run this script once so we sign it with this file
DEP_ST="${RD}/deploy.status"

# If the deployment status file exists, we do not need to run it again
[[ -e ${DEP_ST} && $(cat ${DEP_ST}) = "done" ]] && exit 0 

cd ${RD}/../
git switch pepa-ng
git submodule update --init
cd -
${RD}/install_dependencies.sh
cd libconfuse ; ./autogen.sh ; ./configure ; make clean all ; cd -
echo "done" > ${DEP_ST}

