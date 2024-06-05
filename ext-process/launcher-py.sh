#!/bin/bash

# Copyright (c) 2024 Sapienza University of Rome
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation;
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# Author: Emanuele Giona <giona@di.uniroma1.it> <ORCID: 0000-0003-0871-7156>
#

# Locate ext-process directory from ns-3 installation directory
WKDIR=contrib/ext-process
LOG_PATH="pytest_${BASHPID}.log"

cd "${WKDIR}"

# Redirect stdout and stderr to log file
touch "${LOG_PATH}"
exec 1>>"${LOG_PATH}"
exec 2>&1

echo -e "$(date): external process invoked:\n- command: $0\n- params: '$@'"
echo "=== PROCESS OUTPUT ==="

# Use 'exec' to launch your executable; it will replace this process without 
# creating a subshell.
# ExternalProcess will be able to use ${BASHPID} to kill this process, if necessary.
exec python3 echo.py "$@"
