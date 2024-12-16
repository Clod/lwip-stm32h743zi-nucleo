#!/usr/bin/env python

#
# RTEMS Project (https://www.rtems.org/)
#
# Copyright (c) 2021 Vijay Kumar Banerjee <vijay@rtems.org>.
# All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from __future__ import print_function
from rtems_waf import rtems

try:
    import configparser
except:
    import ConfigParser as configparser

import lwip
import os
import sys
top = '.'

rtems_version = "7"
try:
    import rtems_waf.rtems as rtems
except rtems_waf.DoesNotExist:
    print("error: no rtems_waf git submodule; see README.waf")
    sys.exit(1)


def init(ctx):
    rtems.init(ctx, version=rtems_version, long_commands=True)


def options(opt):
    rtems.options(opt)


def no_unicode(value):
    if sys.version_info[0] > 2:
        return value
    if isinstance(value, unicode):
        return str(value)
    return value


def get_config():
    cp = configparser.ConfigParser()
    filename = "config.ini"
    if filename not in cp.read([filename]):
        return None
    return cp


def get_configured_bsps(cp):
    if not cp:
        return "all"
    bsps = []
    for raw_bsp in cp.sections():
        bsps.append(no_unicode(raw_bsp))
    return ",".join(bsps)


def get_configured_bsp_options(cp, arch, bsp):
    if not cp:
        return {}
    options = {}
    for config_option in cp.items(os.path.join(arch, bsp)):
        opt_name = config_option[0].upper()
        options[opt_name] = config_option[1]
    return options


bsp_opts_target = os.path.join("rtemslwip", "include", "lwipconfig.h")


def bsp_configure(conf, arch_bsp):
    cp = get_config()
    arch = rtems.arch(arch_bsp)
    bsp = rtems.bsp(arch_bsp)
    config_options = get_configured_bsp_options(cp, arch, bsp)
    for key, val in config_options.items():
        conf.define(key, val, quote=False)
    conf.env.include_key = []
    conf.write_config_header(
        os.path.join(arch_bsp, bsp_opts_target),
        guard="CONFIGURED_LWIP_BSP_OPTS_H",
        top=True
    )
    conf.env.include_key = None
    lwip.bsp_configure(conf, arch_bsp)


def configure(conf):
    cp = get_config()
    if conf.options.rtems_bsps == "all":
        conf.options.rtems_bsps = get_configured_bsps(cp)
    rtems.configure(conf, bsp_configure)


def build(bld):
    rtems.build(bld)
    arch_bsp = bld.env.RTEMS_ARCH_BSP
    arch = rtems._arch_from_arch_bsp(arch_bsp)
    bsp = rtems.bsp(arch_bsp)
    install_target = os.path.join(bld.env.PREFIX, arch, bsp, "lib", "include")
    bld.install_files(install_target, bsp_opts_target)
    lwip.build(bld)
