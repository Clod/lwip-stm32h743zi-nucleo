RTEMS lwIP
==========

The rtems-lwip repository serves as a central location to manage integration of
lwIP with RTEMS in a more user-accessible manner and to provide a repository of
network drivers for RTEMS BSPs.


Installation Instructions
-------------------------
  1. Populate the git submodules:

     ```shell
     git submodule init
     git submodule update
     ```

  2. Configure and build

     ```shell
     ./waf configure --prefix=INSTALL_PREFIX
     ./waf
     ./waf install
     ```

     For STM32H743ZI Nucleo board:

     ```shell
     ./waf configure --rtems-bsps=arm/nucleo-h743zi --rtems=/opt/rtems/6.1 --rtems-version=6
     ./waf build
     ```

More `waf` arguments can be found by using:

  ```shell
  ./waf --help
  ```

Further Build Information
-------------------------

The BSPs configured to build may be specified on the waf configure command line
with --rtems-bsps or they may be configured in config.ini as in RTEMS. The
command line option will override the BSPs configured in config.ini, but options
in config.ini will still be applied for enabled BSPs. Any additional
configuration options desired in lwipopts.h may be specified in config.ini under
the appropriate section as key/value pairs like so:

  ```ini
  [aarch64/zynqmp_zu3eg]
  LWIP_IGMP=1
  ZYNQMP_USE_SGMII=1
  ```


File Origins
------------
The sources presented here originate in one of several locations described by
the Source origins below and files and whose license is described by the
LICENSE.md file.  Commits adding such files should include the hash of the
target repository if applicable.


Source origins
--------------
| Directory  | Origin                                        |
| ---        | ---                                           |
| cpsw       | https://github.com/ragunath3252/cpsw-lwip.git |
| defs       | Written specifically for this project.        |
| embeddedsw | https://github.com/Xilinx/embeddedsw.git      |
| lwip       | git://git.savannah.gnu.org/lwip.git           |
| rtemslwip  | Written specfically or pulled from RTEMS      |

