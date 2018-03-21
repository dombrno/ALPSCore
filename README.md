The ALPSCore project, based on the ALPS (Algorithms and Libraries for Physics Simulations) project, provides generic algorithms and utilities for physics problems. It strives to increase software reuse in the physics community.

For copyright see COPYRIGHT.txt
For licensing see LICENSE.txt
For acknowledgment in scientific publications see ACKNOWLEDGE.txt

build on VSC3:
```
module load intel/17 intel-mpi/2017 python/2.7 boost/1.62.0 cmake/3.9.6 hdf5/1.8.18-SERIAL
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../Install -DEIGEN3_INCLUDE_DIR=~/Code/Eigen_334
```
