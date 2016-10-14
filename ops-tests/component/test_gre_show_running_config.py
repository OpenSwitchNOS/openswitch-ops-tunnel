# -*- coding: utf-8 -*-

# (c) Copyright 2015 Hewlett Packard Enterprise Development LP
#
# GNU Zebra is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2, or (at your option) any
# later version.
#
# GNU Zebra is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNU Zebra; see the file COPYING.  If not, write to the Free
# Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
# 02111-1307, USA.


TOPOLOGY = """
# +-------+
# |       |     +-------+
# |  hsw1  <----->  sw1  |
# |       |     +-------+
# +-------+

# Nodes
[type=openswitch name="Switch 1"] sw1
[type=host name="Host 1"] hsw1

# Links
hsw1:if01 -- sw1:if01
"""


def test_gre_ct_show_running_config(topology, step):
    sw1 = topology.get("sw1")
    assert sw1 is not None
    tunnel_number = "19"
    source_ip = "10.1.1.1"
    destination_ip = "20.1.1.1"

    gre_config = ["interface tunnel {} mode gre ipv4".format(tunnel_number),
                  "source ip {}".format(source_ip),
                  "destination ip {}".format(destination_ip),
                  "ip address 172.169.1.1/24",
                  "ttl 150"
                  ]
    step("Applying GRE tunnel configurations...")
    sw1("configure terminal")
    for config in gre_config:
        sw1(config)

    output = sw1("do show running-config interface tunnel {}".format(tunnel_number)) # noqa
    step(output)
    for config in gre_config:
        assert config in output
