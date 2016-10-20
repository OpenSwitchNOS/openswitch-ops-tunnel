from time import sleep


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
    sh_run = ["vtysh", "sh run", "exit"]
    for config in sh_run:
        switch(config)
    step("Configuring Switch Tunnel Interface Completed!")


def configure_inter_hs(host, port, ip, step):
    step("Configuring host")
    host.libs.ip.interface('1', addr=ip, up=True)
    host_config = 'ifconfig -a'
    output = host(host_config, shell='bash')
    step(output)
    step("Configuring host completed!")
