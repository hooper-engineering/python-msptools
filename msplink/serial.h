/*
This file is part of python-msptools.

Python-msptools is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Python-msptools is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with python-msptools.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <stdint.h>
#include <stdio.h>
#include "msplink.h"

int msplink_open(mspdev_t* mdev);
int msplink_close(mspdev_t* mdev);
int msplink_write(mspdev_t* mdev, uint8_t* data, size_t len);
int msplink_read(mspdev_t* mdev, uint8_t* buf, size_t len);
int msplink_bytesavailable(mspdev_t* mdev);
int msplink_waituntilsent(mspdev_t* mdev);
int msplink_clearRxBuffer(mspdev_t* mdev);
