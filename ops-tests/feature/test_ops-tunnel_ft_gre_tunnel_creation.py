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

"""
OpenSwitch Test for GRE related configurations.
"""

from pytest import mark
from tunnel_ft_utils import *
from time import sleep

from pytest import set_trace

TOPOLOGY = """
# +-------+                                                     +-------+
# |       |     +--------+      +--------+      +--------+      |       |
# |  h1   <---->    s1   <----->   s2    <----->    s3    <----->  h2   |
# |       |     +--------+      +--------+      +--------+      |       |
# +-------+                                                     +-------+

# Nodes
[type=openswitch name="OpenSwitch 1"] s1
[type=openswitch name="OpenSwitch 2"] s2
[type=openswitch name="OpenSwitch 3"] s3
[type=host name="Host 1"] h1
[type=host name="Host 2"] h2

# Links
h1:1 -- s1:1
s1:2 -- s2:1
s2:2 -- s3:1
s3:2 -- h2:1
"""

h1_ip = '20.0.0.1'
h2_ip = '30.0.0.1'
s1_int1 = '20.0.0.2'
s1_int2 = '8.0.0.1'
s2_int1 = '8.0.0.2'
s2_int2 = '9.0.0.1'
s3_int1 = '9.0.0.2'
s3_int2 = '30.0.0.2'
host_ip_addr1 = '20.0.0.1'
host_ip_addr1 = '30.0.0.1'
ip_addr1 = "192.168.0.1"
ip_addr2 = "192.168.0.2"
all_mask = '/24'
tun_num = 10


def configure_gre_tunnel(s1, s2, s3, h1, h2, step):

    step("######Configure Switch1######")
    with s1.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.ip_address(s1_int1 + all_mask)
        ctx.no_shutdown()

    with s1.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.ip_address(s1_int2 + all_mask)
        ctx.no_shutdown()

    sleep(15)

    step('######Configuring static routes in SW1 ######')
    with s1.libs.vtysh.Configure() as ctx:
        ctx.ip_route('9.0.0.0/24', '8.0.0.2')

    step("###### Configure Switch2 ######")
    with s2.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.ip_address(s2_int1 + all_mask)
        ctx.no_shutdown()

    with s2.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.ip_address(s2_int2 + all_mask)
        ctx.no_shutdown()

    sleep(10)

    step('###### Configuring static routes in SW2 ######')
    with s2.libs.vtysh.Configure() as ctx:
        ctx.ip_route('30.0.0.0/24', '9.0.0.2')
        ctx.ip_route('20.0.0.0/24', '8.0.0.1')

    step("Configure Switch3")
    with s3.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.ip_address(s3_int1 + all_mask)
        ctx.no_shutdown()

    with s3.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.ip_address(s3_int2 + all_mask)
        ctx.no_shutdown()

    step('###### Configuring static routes in SW3 ######')
    with s3.libs.vtysh.Configure() as ctx:
        ctx.ip_route('8.0.0.0/24', '9.0.0.1')

    step("Configure host1 and host2")

    configure_inter_hs(h1, '1', '20.0.0.1/24', step)
    configure_inter_hs(h2, '1', '30.0.0.1/24', step)
    step("Host config complete")

    step("Ping from Switch1 to Switch3\n")

    ping_output = s1("ping 9.0.0.2")
    step(ping_output)
    assert(ping_output.find('Destination Host Unreachable') == -1)

    ping_output = s3("ping 8.0.0.1")
    step(ping_output)
    assert(ping_output.find('Destination Host Unreachable') == -1)

    step("Configure a tunnel between Switch1 and Switch3")

    step("Configuring tunnel for Switch1")
    config_gre_tunnel(s1, s1_int2, s3_int1, tun_num, ip_addr1, all_mask, step)
    step("Configuring tunnel for Switch3")
    config_gre_tunnel(s3, s3_int1, s1_int2, tun_num, ip_addr2, all_mask, step)

    step("\n\n\n ########## configuring static route for tunnel on switch 1  ##########\n\n\n")
    s1("conf t")
    s1("ip route 30.0.0.0/24 tunnel10")

    step("\n\n\n ########## configuring static route for tunnel on switch 3  ##########\n\n\n")
    s3("conf t")
    s3("ip route 20.0.0.0/24 tunnel10")

    step("\n\n\n ########## configuring static route for Host1  ##########\n\n\n")
    h1("route add -net 30.0.0.0 netmask 255.255.255.0 gw 20.0.0.2")

    step("\n\n\n ########## configuring static route for Host2  ##########\n\n\n")
    h2("route add -net 20.0.0.0 netmask 255.255.255.0 gw 30.0.0.2")


def gre_tunnel_creation_verify():
    step("Ping HOST1 to HOST2")
    sleep(10)
    ping_output = nping(h1, h2_ip, "5", step)
    step(ping_output)
    assert(ping_output.find('Destination Host Unreachable') == -1)


def test_gre_tunnel_creation(topology, step):
    """
    Test ping between 2 hosts connected through GRE tunnel
    between s1 and s3 OpenSwitch switches.

    Build a topology of 3 switches and two hosts and connect
    the hosts to the switches.
    Setup a GRE tunnel between switches s1 and s3.
    Ping from host 1 to host 2.
    """

    s1 = topology.get('s1')
    s2 = topology.get('s2')
    s3 = topology.get('s3')
    h1 = topology.get('h1')
    h2 = topology.get('h2')

    assert s1 is not None
    assert s2 is not None
    assert s3 is not None
    assert h1 is not None
    assert h2 is not None

    step("##### 1. Configure the network #########")
    configure_gre_tunnel(s1, s2, s3, h1, h2, step)
    gre_tunnel_creation_verify()
