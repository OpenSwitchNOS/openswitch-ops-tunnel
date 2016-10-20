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
OpenSwitch Test for VxLAN related configurations.
"""

from tunnel_ft_utils import config_switch, config_vni, config_vlan, \
    config_vlan_vni_mapping, config_vlan_access_iface, config_l3_iface, \
    config_tunnel_iface, configure_inter_hs
from time import sleep

TOPOLOGY = """
# +-------+                                    +-------+
# |       |     +--------+      +--------+     |       |
# |  h1   <---->    s1   <----->   s2    <----->  h2   |
# |       |     +--------+      +--------+     |       |
# +-------+                                    +-------+

# Nodes
[type=openswitch name="OpenSwitch 1"] s1
[type=openswitch name="OpenSwitch 1"] s2
[type=host name="Host 1"] h1
[type=host name="Host 1"] h2

#ports
[force_name=oobm] s1:sp1

# Links
h1:1 -- s1:1
s1:2 -- s2:2
s2:1 -- h2:1
"""

s1_ip = '20.0.0.1'
s2_ip = '20.0.0.2'
h1_ip = '10.0.0.10'
h2_ip = '10.0.0.20'
mask = '24'
num_pack = '10'
vlan = '21'
vni = '1760'
tun_num = '10'


def nping(host, dest, times, step):
    step("Starting Ping...")
    # send contrived traffic
    # no broadcast, all unicast with well formed mac addrs
    ping_cmd = ('ping -c ' + times + ' ' + dest)
    step('Executing ' + ping_cmd)

    output = host(ping_cmd, shell='bash')

    return(output)


def test_vxlan(topology, step):
    """
    # Test ping between two hosts without VxLAN tunnel creation.
    # Test ping between two hosts connected through VxLAN tunnel.

    Build a topology of two switches and two hosts and connect
    the hosts to the switches.
    Setup a VxLAN between switches and VLANs between ports
    and switches.
    Ping from host 1 to host 2.
    """

    s1 = topology.get('s1')
    s2 = topology.get('s2')
    h1 = topology.get('h1')
    h2 = topology.get('h2')

    assert s1 is not None
    assert s2 is not None
    assert h1 is not None
    assert h2 is not None

    # wait for switchd to come up
    sleep(10)
    config_vlan(s1, vlan, step)
    config_vlan_access_iface(s1, vlan, '1', step)
    config_l3_iface(s1, '2', s1_ip, mask, step)

    config_vlan(s2, vlan, step)
    config_vlan_access_iface(s2, vlan, '1', step)
    config_l3_iface(s2, '2', s2_ip, mask, step)

    # Ping between the two switches
    step("##### Trying to ping between the two switches #####")
    output = s1('ip netns exec swns ping -c ' + num_pack + ' ' \
                + s2_ip, shell='bash')

    step(output)
    assert(output.find('Destination Host Unreachable') == -1)

    length_pack = len(num_pack)
    index_t = output.index('packets transmitted') - 1
    transmitted = (output[index_t - length_pack:index_t])
    index_r = output.index('received') - 1
    received = (output[index_r - length_pack:index_r])

    assert(transmitted == received)

    # configure VNI and VLAN to VNI mapping
    config_vni(s1, vni, step)
    config_vlan_vni_mapping(s1, vlan, vni, step)
    config_vni(s2, vni, step)
    config_vlan_vni_mapping(s2, vlan, vni, step)

    # configure_inter_hs(host, port, ip, step)
    configure_inter_hs(h1, '1', h1_ip + '/' + mask, step)
    configure_inter_hs(h2, '1', h2_ip + '/' + mask, step)

    # Negative test
    # Trying to ping without a VxLAN tunnel creation
    step("##### Trying to ping between two hosts without tunnel creation #####")
    neg_ping_output = nping(h1, h2_ip, num_pack, step)
    step(neg_ping_output)

    assert(neg_ping_output.find('Destination Host Unreachable') != -1)

    length_pack = len(num_pack)
    index_r = neg_ping_output.index('received') - 1
    received = (neg_ping_output[index_r - length_pack:index_r])
    received = [int(s) for s in received.split() if s.isdigit()]

    assert(received[0] == 0)

    # wait for mappings to get resolve
    sleep(5)

    # Configure tunnel interfaces on both the switches
    config_tunnel_iface(s1, vni, s1_ip, s2_ip, tun_num, step)
    config_tunnel_iface(s2, vni, s2_ip, s1_ip, tun_num, step)

    # Wait for tunnel interface to get created
    sleep(5)

    step("##### Trying to ping between the two hosts #####")
    ping_output = nping(h1, h2_ip, num_pack, step)

    step(ping_output)
    assert(ping_output.find('Destination Host Unreachable') == -1)

    length_pack = len(num_pack)
    index_t = ping_output.index('packets transmitted') - 1
    transmitted = (ping_output[index_t - length_pack:index_t])
    index_r = ping_output.index('received') - 1
    received = (ping_output[index_r - length_pack:index_r])

    assert(transmitted == received)
