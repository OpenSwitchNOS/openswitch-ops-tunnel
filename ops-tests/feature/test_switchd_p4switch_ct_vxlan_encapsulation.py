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
OpenSwitch Test for VxLAN encapsulation verification
"""
import threading
from logging import getLogger
from tunnel_ft_utils import *
from time import sleep

TOPOLOGY = """
# +-------+                    +-------+
# |       |     +--------+     |       |
# |  h1   <---->    s1   <---->|  h2   |
# |       |     +--------+     |       |
# +-------+                    +-------+

# Nodes
[type=openswitch name="OpenSwitch 1"] s1
[type=host name="Host 1"] h1
[type=host name="Host 1"] h2

#ports
[force_name=oobm] s1:sp1

# Links
h1:1 -- s1:1
s1:2 -- h2:1
"""

s1_ip = '10.0.0.1'
h1_ip = '10.0.0.10'
h2_ip = '10.0.0.20'
mask = '24'
num_pack = '11'
vlan = '21'
vni = '1760'
tun_num = '10'
nping_succeeded = False


class GeneratorThread (threading.Thread):
    '''Generate traffic from the host'''
    def __init__(self, thread_id, host, src_ip, dst_ip):
        threading.Thread.__init__(self)
        self.thread_id = thread_id
        self.host = host
        self.src_ip = src_ip
        self.dst_ip = dst_ip
        self.daemon = False


    def run(self):
        print("Starting ping test")
        ping_cmd = ('ping -c 2 ' + h2_ip)
        print('Executing ' + ping_cmd)

        output = self.host(ping_cmd, shell='bash')


class ReceiverThread (threading.Thread):
    '''Receive traffic via tcpdump
       Update nping_succeeded when packets are found
    '''

    def __init__(self, thread_id, host, src_ip, dst_ip):
        threading.Thread.__init__(self)
        self.thread_id = thread_id
        self.host = host
        self.src_ip = src_ip
        self.dst_ip = dst_ip
        self.daemon = False


    def run(self):
        global nping_succeeded
        print("Starting tcpdump...")
        dumped_traffic = self.host('ip netns exec emulns '
                                   'tcpdump -i ' + self.host.ports['2'] +
                                   ' -c 1 udp port 4789',
                                   shell='bash')
        print(dumped_traffic)
        print("Finished tcpdump!!!")
        m = dumped_traffic.find('VXLAN')
        n = dumped_traffic.find('vni ' + vni)
        if (m != -1 and n != -1):
            nping_succeeded = True


def test_vxlan(topology, step):
    """
    Test ping between 2 hosts connected through VxLAN tunnel.

    Build a topology of two switches and two hosts and connect
    the hosts to the switches.
    Setup a VxLAN between switches and VLANs between ports
    and switches.
    Ping from host 1 to host 2.
    """

    s1 = topology.get('s1')
    h1 = topology.get('h1')
    h2 = topology.get('h2')

    assert s1 is not None
    assert h1 is not None
    assert h2 is not None

    # wait for switchd to come up
    sleep(10)

    # configure_inter_hs(host, port, ip, step)
    configure_inter_hs(h1, '1', h1_ip + '/' + mask, step)
    configure_inter_hs(h2, '1', h2_ip + '/' + mask, step)
    config_vlan(s1, vlan, step)
    config_vlan_access_iface(s1, vlan, '1', step)
    config_l3_iface(s1, '2', s1_ip, mask, step)

    # Ping between the two switches
    step("Trying to ping to h2 from s1")
    output = s1('ip netns exec swns ping -c ' + num_pack + ' ' \
        + h2_ip, shell='bash')

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

    # wait for mappings to get resolve
    sleep(5)
    # Configure tunnel interfaces on both the switches
    config_tunnel_iface(s1, vni, s1_ip, h2_ip, tun_num, step)
    # Wait for tunnel interface to get created
    sleep(5)
    h2_thread = ReceiverThread('2', s1, h1_ip, h2_ip)
    h1_thread = GeneratorThread('1', h1, h1_ip, h2_ip)

    h2_thread.start()
    sleep(1)
    h1_thread.start()

    h1_thread.join()
    h2_thread.join()

    assert(nping_succeeded)
