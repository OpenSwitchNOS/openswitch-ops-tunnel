# -*- coding: utf-8 -*-
#
# (c) Copyright 2016 Hewlett Packard Enterprise Development LP
#
#  Licensed under the Apache License, Version 2.0 (the "License"); you may
#  not use this file except in compliance with the License. You may obtain
#  a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#  License for the specific language governing permissions and limitations
#  under the License.

############################################################################

"""
Tunnel utils
"""

from time import sleep


def nping(host, dest, times, step):
    step("Starting Ping...")
    # send contrived traffic
    # no broadcast, all unicast with well formed mac addrs
    ping_cmd = ('ping -c ' + times + ' ' + dest)
    step('Executing ' + ping_cmd)

    output = host(ping_cmd, shell='bash')

    return(output)


def config_gre_tunnel(switch, s_ip, d_ip, tun_num, ip_addr, mask, step):
    step("Configuring GRE tunnel")
    switch_config = ["vtysh",
                     "configure terminal",
                     "interface tunnel {} mode gre ipv4".format(tun_num),
                     "ip address {}{}".format(ip_addr, mask),
                     "no shut",
                     "source ip {}".format(s_ip),
                     "destination ip {}". format(d_ip),
                     "exit"]
    config_switch(switch, switch_config, step)

    sh_run = ["vtysh", "sh run", "exit"]
    for config in sh_run:
        switch(config)
    step("Configuring Switch Tunnel Interface Completed!")


def config_switch(switch, switch_config, step):
    for config in switch_config:
        switch(config)
    sleep(2)


def config_vni(switch, vni, step):
    step("Configuring Switch VNI")
    switch_config = ["vtysh",
                     "configure terminal",
                     "vni {}".format(vni),
                     "exit",]
    config_switch(switch, switch_config, step)
    step("Configuring Switch VNI Completed!")


def config_vlan(switch, vlan, step):
    step("Configuring Switch VLAN")
    switch_config = ["vtysh",
                     "configure terminal",
                     "vlan {}".format(vlan),
                     "no shut",
                     "exit",]
    config_switch(switch, switch_config, step)
    step("Configuring Switch VLAN Completed!")


def config_vlan_vni_mapping(switch, vlan, vni, step):
    step("Configuring Switch VLAN to VNI Mapping")
    switch_config = ["vtysh",
                     "configure terminal",
                     "vxlan vlan {} vni {}".format(vlan, vni),
                     "exit",]
    config_switch(switch, switch_config, step)
    step("Configuring Switch VLAN to VNI Mapping Completed!")


def config_vlan_access_iface(switch, vlan, iface, step):
    step("Configuring Switch VLAN Access Interface")
    switch_config = ["vtysh",
                     "configure terminal",
                     "interface {}".format(iface),
                     "no routing",
                     "vlan access {}".format(vlan),
                     "no shut",
                     "exit",]
    config_switch(switch, switch_config, step)
    step("Configuring Switch VLAN Access Interface Completed!")


def config_l3_iface(switch, iface, ip, mask, step):
    step("Configuring Switch L3 Interface")
    switch_config = ["vtysh",
                     "configure terminal",
                     "interface {}".format(iface),
                     "ip address {}/{}".format(ip, mask),
                     "no shut",
                     "exit",]
    config_switch(switch, switch_config, step)
    step("Configuring Switch L3 Interface Completed!")


def config_tunnel_iface(switch, vni_num, s_ip, d_ip, tun_num, step):
    step("Configuring Switch Tunnel Interface")
    switch_config = ["vtysh",
                     "configure terminal",
                     "interface tunnel {} mode vxlan".format(tun_num),
                     "source ip {}".format(s_ip),
                     "destination ip {}". format(d_ip),
                     "vxlan-vni {}".format(vni_num),
                     "exit"]
    config_switch(switch, switch_config, step)
    step("Configuring Switch Tunnel Interface Completed!")


def configure_inter_hs(host, port, ip, step):
    step("Configuring host")
    host.libs.ip.interface(port, addr=ip, up=True)
    host_config = 'ifconfig -a'
    output = host(host_config, shell='bash')
    step("Configuring host completed!")
